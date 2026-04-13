#include "ConfigManager.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QTimer>
static const QStringList GKEY_NAMES = {
    "G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","G19","G20"
};
static const QStringList MKEY_NAMES = { "BTN_LEFT","BTN_RIGHT","MIDDLE","TILT_LEFT","TILT_RIGHT" };

static const QStringList ALL_BUTTONS = []() {
    QStringList l;
    for (auto &g : GKEY_NAMES) l << g;
    for (auto &g : GKEY_NAMES) l << ("SHIFT_" + g);
    for (auto &m : MKEY_NAMES) l << m;
    return l;
}();

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
    QString cfgDir = QDir::homePath() + "/.config/g600d";
    QDir().mkpath(cfgDir);
    m_path = cfgDir + "/g600d.conf";

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ConfigManager::onFileChanged);
    load();
}

QVariantMap ConfigManager::defaultProfile(const QString &name) const {
    QVariantMap p;
    p["name"]         = name;
    p["abs_misc"]     = -1;
    p["led_r"]        = 255;
    p["led_g"]        = 255;
    p["led_b"]        = 255;
    p["led_effect"]   = "solid";
    p["led_duration"] = 4;
    p["led_enabled"]  = false;
    p["dpi_1"]        = 1200;
    p["dpi_2"]        = 0;
    p["dpi_3"]        = 0;
    p["dpi_4"]        = 0;
    p["dpi_shift"]    = 0;
    p["dpi_default"]  = 1;
    p["dpi_enabled"]  = false;

    QVariantMap bindings;
    QStringList fkeys = {"F13","F14","F15","F16","F17","F18","F19","F20","F21","F22","F23","F24"};
    QStringList sfkeys= {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    for (int i = 0; i < 12; i++) {
        bindings[GKEY_NAMES[i]]           = fkeys[i];
        bindings["SHIFT_" + GKEY_NAMES[i]] = sfkeys[i];
    }
    bindings["BTN_LEFT"]   = "BTN_LEFT";
    bindings["BTN_RIGHT"]  = "BTN_RIGHT";
    bindings["MIDDLE"]     = "BTN_MIDDLE";
    bindings["TILT_LEFT"]  = "BTN_SIDE";
    bindings["TILT_RIGHT"] = "BTN_EXTRA";
    p["bindings"] = bindings;
    return p;
}

void ConfigManager::load() {
    QFile f(m_path);
    if (!f.exists()) {
        QVariantMap p1 = defaultProfile("default");  p1["abs_misc"] = 32;
        QVariantMap p2 = defaultProfile("profile2"); p2["abs_misc"] = 64;
        QVariantMap p3 = defaultProfile("profile3"); p3["abs_misc"] = 128;
        m_profiles = { p1, p2, p3 };
        save();
        emit profilesChanged();
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) return;
    QTextStream in(&f);

    m_profiles.clear();
    QVariantMap cur;
    QVariantMap bindings;

    auto finishProfile = [&]() {
        if (!cur.isEmpty()) {
            cur["bindings"] = bindings;
            m_profiles.append(cur);
        }
    };

    while (!in.atEnd()) {
        QString line = in.readLine();
        int hash = line.indexOf('#');
        if (hash >= 0) line = line.left(hash);
        line = line.trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith('[') && line.contains(']')) {
            finishProfile();
            cur = defaultProfile(line.mid(1, line.indexOf(']') - 1));
            cur["led_enabled"] = false;
            bindings = cur["bindings"].toMap();
            continue;
        }

        int eq = line.indexOf('=');
        if (eq < 0) continue;
        QString key = line.left(eq).trimmed().toLower();
        QString val = line.mid(eq + 1).trimmed();

        if (cur.isEmpty()) continue;

        if (key == "dpi_1")        { cur["dpi_1"] = val.toInt(); cur["dpi_enabled"] = true; continue; }
        if (key == "dpi_2")        { cur["dpi_2"] = val.toInt(); cur["dpi_enabled"] = true; continue; }
        if (key == "dpi_3")        { cur["dpi_3"] = val.toInt(); cur["dpi_enabled"] = true; continue; }
        if (key == "dpi_4")        { cur["dpi_4"] = val.toInt(); cur["dpi_enabled"] = true; continue; }
        if (key == "dpi_shift")    { cur["dpi_shift"] = val.toInt(); cur["dpi_enabled"] = true; continue; }
        if (key == "dpi_default")  { cur["dpi_default"] = val.toInt(); cur["dpi_enabled"] = true; continue; }
        if (key == "abs_misc")     { cur["abs_misc"] = val.toInt(); continue; }
        if (key == "led_r")        { cur["led_r"] = val.toInt(); cur["led_enabled"] = true; continue; }
        if (key == "led_g")        { cur["led_g"] = val.toInt(); cur["led_enabled"] = true; continue; }
        if (key == "led_b")        { cur["led_b"] = val.toInt(); cur["led_enabled"] = true; continue; }
        if (key == "led_effect")   { cur["led_effect"] = val.toLower(); cur["led_enabled"] = true; continue; }
        if (key == "led_duration") { cur["led_duration"] = val.toInt(); cur["led_enabled"] = true; continue; }

        /* binding */
        QString upperKey = line.left(eq).trimmed().toUpper();
        if (ALL_BUTTONS.contains(upperKey))
            bindings[upperKey] = val;
    }
    finishProfile();

    if (m_profiles.isEmpty())
        m_profiles = { defaultProfile("default") };

    f.close();

    m_watcher.removePaths(m_watcher.files());
    m_watcher.addPath(m_path);

    emit profilesChanged();
    emit configReloaded();
}

void ConfigManager::save() {
    m_saving = true;
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { qWarning() << "Cannot write config"; return; }
    QTextStream out(&f);

    out << "# G600 Daemon Config\n";
    out << "# Simple rebind: G9 = F13\n";
    out << "# Macro: G9 = macro:<press|release>:<once|repeat|toggle>: KEY, Nms, KEY+KEY\n\n";

    for (const auto &pv : m_profiles) {
        QVariantMap p = pv.toMap();
        out << "[" << p["name"].toString() << "]\n";
        out << "abs_misc = " << p["abs_misc"].toInt() << "\n";

        if (p["dpi_enabled"].toBool()) {
            out << "dpi_1 = "       << p["dpi_1"].toInt()       << "\n";
            out << "dpi_2 = "       << p["dpi_2"].toInt()       << "\n";
            out << "dpi_3 = "       << p["dpi_3"].toInt()       << "\n";
            out << "dpi_4 = "       << p["dpi_4"].toInt()       << "\n";
            out << "dpi_shift = "   << p["dpi_shift"].toInt()   << "\n";
            out << "dpi_default = " << p["dpi_default"].toInt() << "\n";
        }
        if (p["led_enabled"].toBool()) {
            out << "led_r = "        << p["led_r"].toInt()        << "\n";
            out << "led_g = "        << p["led_g"].toInt()        << "\n";
            out << "led_b = "        << p["led_b"].toInt()        << "\n";
            out << "led_effect = "   << p["led_effect"].toString() << "\n";
            out << "led_duration = " << p["led_duration"].toInt() << "\n";
        }
        out << "\n";

        QVariantMap bindings = p["bindings"].toMap();
        for (auto &g : GKEY_NAMES)
            out << g << " = " << bindings.value(g, "NONE").toString() << "\n";
        out << "\n";
        for (auto &g : GKEY_NAMES)
            out << "SHIFT_" << g << " = " << bindings.value("SHIFT_" + g, "NONE").toString() << "\n";
        out << "\n";
        for (auto &m : MKEY_NAMES)
            out << m << " = " << bindings.value(m, "NONE").toString() << "\n";
        out << "\n";
    }
    f.close();

    m_watcher.removePaths(m_watcher.files());
    m_watcher.addPath(m_path);
    QTimer::singleShot(300, this, [this]() { m_saving = false; });
}

void ConfigManager::setCurrentProfile(int idx) {
    if (idx < 0 || idx >= m_profiles.size()) return;
    m_currentProfile = idx;
    emit currentProfileChanged();
}

void ConfigManager::addProfile(const QString &name) {
    m_profiles.append(defaultProfile(name));
    emit profilesChanged();
}

void ConfigManager::removeProfile(int idx) {
    if (m_profiles.size() <= 1 || idx < 0 || idx >= m_profiles.size()) return;
    m_profiles.removeAt(idx);
    if (m_currentProfile >= m_profiles.size()) m_currentProfile = m_profiles.size() - 1;
    emit profilesChanged();
    emit currentProfileChanged();
}

void ConfigManager::setBinding(int profileIdx, const QString &button, const QString &value) {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return;
    QVariantMap p = m_profiles[profileIdx].toMap();
    QVariantMap b = p["bindings"].toMap();
    b[button.toUpper()] = value;
    p["bindings"] = b;
    m_profiles[profileIdx] = p;
    emit profilesChanged();
}

QString ConfigManager::getBinding(int profileIdx, const QString &button) const {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return {};
    return m_profiles[profileIdx].toMap()["bindings"].toMap().value(button.toUpper()).toString();
}

void ConfigManager::setLed(int profileIdx, int r, int g, int b, const QString &effect, int duration) {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return;
    QVariantMap p = m_profiles[profileIdx].toMap();
    p["led_r"] = r; p["led_g"] = g; p["led_b"] = b;
    p["led_effect"] = effect; p["led_duration"] = duration;
    p["led_enabled"] = true;
    m_profiles[profileIdx] = p;
    emit profilesChanged();
}

QVariantMap ConfigManager::getLed(int profileIdx) const {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return {};
    QVariantMap p = m_profiles[profileIdx].toMap();
    return {
        {"r", p["led_r"]}, {"g", p["led_g"]}, {"b", p["led_b"]},
        {"effect", p["led_effect"]}, {"duration", p["led_duration"]},
        {"enabled", p["led_enabled"]}
    };
}

void ConfigManager::setProfileName(int idx, const QString &name) {
    if (idx < 0 || idx >= m_profiles.size()) return;
    QVariantMap p = m_profiles[idx].toMap();
    p["name"] = name;
    m_profiles[idx] = p;
    emit profilesChanged();
}

void ConfigManager::setAbsMisc(int profileIdx, int val) {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return;
    QVariantMap p = m_profiles[profileIdx].toMap();
    p["abs_misc"] = val;
    m_profiles[profileIdx] = p;
    emit profilesChanged();
}

void ConfigManager::onFileChanged(const QString &path) {
    Q_UNUSED(path)
    if (m_saving) { m_watcher.addPath(m_path); return; }
    QTimer::singleShot(100, this, [this]() {
        if (!m_saving) load();
    });
}

void ConfigManager::setDpi(int profileIdx, int d1, int d2, int d3, int d4, int def, int shift) {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return;
    QVariantMap p = m_profiles[profileIdx].toMap();
    p["dpi_1"] = d1; p["dpi_2"] = d2; p["dpi_3"] = d3; p["dpi_4"] = d4;
    p["dpi_default"] = def; p["dpi_shift"] = shift;
    p["dpi_enabled"] = true;
    m_profiles[profileIdx] = p;
    emit profilesChanged();
}

QVariantMap ConfigManager::getDpi(int profileIdx) const {
    if (profileIdx < 0 || profileIdx >= m_profiles.size()) return {};
    QVariantMap p = m_profiles[profileIdx].toMap();
    return {
        {"d1", p["dpi_1"]}, {"d2", p["dpi_2"]},
        {"d3", p["dpi_3"]}, {"d4", p["dpi_4"]},
        {"def", p["dpi_default"]}, {"shift", p["dpi_shift"]},
        {"enabled", p["dpi_enabled"]}
    };
}
