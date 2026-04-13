#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QString>

class ConfigManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(int currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    QVariantList profiles() const { return m_profiles; }
    int currentProfile() const { return m_currentProfile; }
    void setCurrentProfile(int idx);

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void addProfile(const QString &name);
    Q_INVOKABLE void removeProfile(int idx);
    Q_INVOKABLE void setBinding(int profileIdx, const QString &button, const QString &value);
    Q_INVOKABLE QString getBinding(int profileIdx, const QString &button) const;
    Q_INVOKABLE void setLed(int profileIdx, int r, int g, int b, const QString &effect, int duration);
    Q_INVOKABLE QVariantMap getLed(int profileIdx) const;
    Q_INVOKABLE void setProfileName(int idx, const QString &name);
    Q_INVOKABLE void setAbsMisc(int profileIdx, int val);
    Q_INVOKABLE void setDpi(int profileIdx, int d1, int d2, int d3, int d4, int def, int shift);
    Q_INVOKABLE QVariantMap getDpi(int profileIdx) const;

signals:
    void profilesChanged();
    void currentProfileChanged();
    void configReloaded();

private:
    QString m_path;
    QVariantList m_profiles;  // list of QVariantMap
    int m_currentProfile = 0;
    QFileSystemWatcher m_watcher;
    bool m_saving = false;

    QVariantMap defaultProfile(const QString &name) const;
    void parseFile(const QString &path);
    void writeFile(const QString &path) const;

private slots:
    void onFileChanged(const QString &path);
};
