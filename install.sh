#!/usr/bin/env bash
set -euo pipefail

# ─── G600 Drivers Install Script ───────────────────────────────────────────────
# Builds and installs:
#   - g600d daemon        → /usr/local/bin/g600d
#   - g600-config GUI     → /usr/local/bin/g600-config
#   - default config      → ~/.config/g600/config  (if not already present)
#   - udev rule           → /etc/udev/rules.d/99-g600.rules
#   - user systemd unit   → ~/.config/systemd/user/g600d.service
#
# Usage:
#   sudo -E ./install.sh
#
# The -E flag is required so that $HOME and $USER refer to your account,
# not root's, ensuring config/service files land in the right place.
# ───────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BIN_DIR="/usr/local/bin"
UDEV_RULES_DIR="/etc/udev/rules.d"

# Resolve the real (non-root) user even when run via sudo
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

SYSTEMD_USER_DIR="$REAL_HOME/.config/systemd/user"

CONFIG_DIR="$REAL_HOME/.config/g600"
CONFIG_FILE="$CONFIG_DIR/config"

DESKTOP_DIR="$REAL_HOME/.local/share/applications"

# ─── helpers ───────────────────────────────────────────────────────────────────

info()    { printf '\e[1;34m  ==> \e[0m%s\n' "$*"; }
success() { printf '\e[1;32m  ✔  \e[0m%s\n' "$*"; }
warn()    { printf '\e[1;33m  !  \e[0m%s\n' "$*"; }
die()     { printf '\e[1;31m  ✘  \e[0m%s\n' "$*" >&2; exit 1; }

require_root() {
    [[ $EUID -eq 0 ]] || die "This script must be run as root. Use: sudo -E $0"
}

# ─── 1. Build ──────────────────────────────────────────────────────────────────

build() {
    info "Building with meson / ninja …"
    cd "$SCRIPT_DIR"

    if [[ ! -d build ]]; then
        meson setup build --buildtype=release
    else
        meson setup --reconfigure build --buildtype=release
    fi

    ninja -C build
    success "Build complete."
}

# ─── 2. Install binaries ───────────────────────────────────────────────────────

install_binaries() {
    info "Installing binaries to $BIN_DIR …"

    # Remove stale binaries from the old install location if present
    rm -f /usr/bin/g600d /usr/bin/g600-config

    install -Dm755 "$SCRIPT_DIR/build/g600d"        "$BIN_DIR/g600d"
    install -Dm755 "$SCRIPT_DIR/build/g600-config"  "$BIN_DIR/g600-config"

    success "Binaries installed."
}

# ─── 3. Default config ─────────────────────────────────────────────────────────

install_config() {
    info "Setting up default config for '$REAL_USER' …"
    mkdir -p "$CONFIG_DIR"
    chown "$REAL_USER" "$CONFIG_DIR"

    if [[ -f "$CONFIG_FILE" ]]; then
        warn "Config already exists at $CONFIG_FILE — not overwriting."
    else
        install -Dm644 "$SCRIPT_DIR/default.conf" "$CONFIG_FILE"
        chown "$REAL_USER" "$CONFIG_FILE"
        success "Default config installed to $CONFIG_FILE"
    fi
}

# ─── 4. udev rules ─────────────────────────────────────────────────────────────
#
#  - Grants the 'input' group rw access to the G600 hidraw interface so the
#    daemon can open it without root.
#  - Grants the 'input' group rw access to /dev/uinput so the daemon can
#    create its virtual keyboard/mouse device without root.
# ───────────────────────────────────────────────────────────────────────────────

install_udev_rules() {
    info "Installing udev rules …"

    cat > "$UDEV_RULES_DIR/99-g600.rules" <<'EOF'
# Logitech G600 Gaming Mouse — grant 'input' group access to hidraw interface
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="c24a", GROUP="input", MODE="0660"

# Allow 'input' group to use /dev/uinput (needed to create virtual input device)
KERNEL=="uinput", GROUP="input", MODE="0660"
EOF

    udevadm control --reload-rules
    udevadm trigger --subsystem-match=hidraw 2>/dev/null || true
    udevadm trigger --subsystem-match=misc    2>/dev/null || true

    success "udev rules installed and reloaded."
}

# ─── 5. Add user to the 'input' group ─────────────────────────────────────────

ensure_input_group() {
    info "Checking that '$REAL_USER' is in the 'input' group …"

    if id -nG "$REAL_USER" | grep -qw input; then
        warn "'$REAL_USER' is already in the 'input' group."
    else
        usermod -aG input "$REAL_USER"
        success "Added '$REAL_USER' to the 'input' group."
        warn "You must log out and back in (or reboot) for the group change to take effect."
    fi
}

# ─── 6. User systemd service ───────────────────────────────────────────────────
#
#  Runs g600d as the real user so it reads ~/.config/g600/config.
#  A user service (rather than a system service) avoids needing to hardcode
#  the username or home directory into the unit file.
# ───────────────────────────────────────────────────────────────────────────────

install_service() {
    info "Installing user systemd service for '$REAL_USER' …"

    # Remove old system service if it was installed by a previous version
    if [[ -f /etc/systemd/system/g600d.service ]]; then
        systemctl disable --now g600d.service 2>/dev/null || true
        rm -f /etc/systemd/system/g600d.service
        systemctl daemon-reload
        info "Removed old system-level g600d service."
    fi

    mkdir -p "$SYSTEMD_USER_DIR"
    cat > "$SYSTEMD_USER_DIR/g600d.service" <<EOF
[Unit]
Description=G600 Gaming Mouse Daemon
After=default.target

[Service]
Type=simple
ExecStart=$BIN_DIR/g600d
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
EOF
    chown "$REAL_USER" "$SYSTEMD_USER_DIR/g600d.service"

    # Enable and (re)start as the real user
    local uid
    uid="$(id -u "$REAL_USER")"
    sudo -u "$REAL_USER" \
        XDG_RUNTIME_DIR="/run/user/$uid" \
        systemctl --user daemon-reload
    sudo -u "$REAL_USER" \
        XDG_RUNTIME_DIR="/run/user/$uid" \
        systemctl --user enable --now g600d.service

    sleep 1
    if sudo -u "$REAL_USER" \
            XDG_RUNTIME_DIR="/run/user/$uid" \
            systemctl --user is-active --quiet g600d.service; then
        success "g600d user service is running."
    else
        warn "g600d service failed to start. Check: journalctl --user -u g600d -n 30"
    fi

    success "User service installed and enabled."
}

# ─── 7. Desktop file for the config GUI ───────────────────────────────────────

install_desktop_file() {
    info "Installing .desktop file …"
    mkdir -p "$DESKTOP_DIR"

    cat > "$DESKTOP_DIR/g600-config.desktop" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=G600 Config
GenericName=Gaming Mouse Configuration
Comment=Configure profiles, macros, LEDs and DPI for the Logitech G600
Exec=$BIN_DIR/g600-config
Icon=input-gaming
Categories=Settings;HardwareSettings;
Keywords=mouse;gaming;logitech;g600;macro;
StartupNotify=true
EOF

    chown "$REAL_USER" "$DESKTOP_DIR/g600-config.desktop"

    if command -v update-desktop-database &>/dev/null; then
        update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
    fi

    success "Desktop file installed."
}

# ─── usage / main ──────────────────────────────────────────────────────────────

usage() {
    cat <<EOF
Usage: sudo -E $0 [OPTIONS]

Options:
  --build-only   Only build, do not install anything
  --no-build     Skip the build step (use existing build/ artefacts)
  --help         Show this help

Steps performed:
  1. Build (meson + ninja)
  2. Install binaries to $BIN_DIR
  3. Install default config (skipped if one already exists)
  4. Install udev rules (hidraw + uinput access for 'input' group)
  5. Add $REAL_USER to the 'input' group
  6. Install & enable system systemd service (runs as $REAL_USER)
  7. Install .desktop file for g600-config

The -E flag on sudo is required so \$HOME and \$USER resolve to your
account rather than root's.
EOF
}

main() {
    local do_build=true

    for arg in "$@"; do
        case "$arg" in
            --build-only) require_root; build; exit 0 ;;
            --no-build)   do_build=false ;;
            --help|-h)    usage; exit 0 ;;
            *) die "Unknown option: $arg. Use --help for usage." ;;
        esac
    done

    require_root

    echo ""
    echo "  ╔══════════════════════════════════════╗"
    echo "  ║   G600 Drivers — Install Script      ║"
    echo "  ╚══════════════════════════════════════╝"
    echo ""
    info "Installing for user: $REAL_USER  (home: $REAL_HOME)"
    echo ""

    $do_build      && build
    install_binaries
    install_config
    install_udev_rules
    ensure_input_group
    install_service
    install_desktop_file

    echo ""
    success "Installation complete!"
    echo ""
    echo "  Next steps:"
    echo "    • Replug your G600 so the udev rules take effect."
    if ! id -nG "$REAL_USER" | grep -qw input; then
        echo "    • Log out and back in (or reboot) — you were just added to 'input'."
    fi
    echo "    • The daemon is already running:  systemctl --user status g600d"
    echo "    • To tweak settings, run:         g600-config"
    echo ""
}

main "$@"
