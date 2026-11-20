#include "train/model_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace zp {

ModelRegistry &ModelRegistry::instance()
{
    static ModelRegistry s;
    return s;
}

ModelRegistry::ModelRegistry()
{
    m_root = QCoreApplication::applicationDirPath() + QStringLiteral("/models");
    QDir().mkpath(versionsRoot());
}

QString ModelRegistry::root() const { return m_root; }
QString ModelRegistry::versionsRoot() const { return QDir(m_root).filePath(QStringLiteral("versions")); }
QString ModelRegistry::currentJsonPath() const { return QDir(m_root).filePath(QStringLiteral("current.json")); }

QVector<ModelVersion> ModelRegistry::versions() const
{
    QVector<ModelVersion> out;
    QDir vroot(versionsRoot());
    const QStringList names = vroot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QDir d(vroot.filePath(name));
        const QString model = d.filePath(QStringLiteral("model.onnx"));
        if (!QFileInfo::exists(model)) {
            continue;
        }
        ModelVersion v;
        v.name = name;
        v.dir = d.absolutePath();
        v.modelPath = model;
        v.metricsPath = d.filePath(QStringLiteral("metrics.json"));
        if (QFileInfo::exists(v.metricsPath)) {
            QFile f(v.metricsPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                v.metrics = QJsonDocument::fromJson(f.readAll()).object();
                v.hasMetrics = !v.metrics.isEmpty();
            }
        }
        out.append(v);
    }
    return out;
}

QString ModelRegistry::currentVersion() const
{
    QFile f(currentJsonPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    return o.value(QStringLiteral("version")).toString();
}

bool ModelRegistry::setCurrent(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    QDir().mkpath(root());
    QFile f(currentJsonPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QJsonObject o;
    o.insert(QStringLiteral("version"), name);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return true;
}

bool ModelRegistry::addVersion(const QString &name, const QString &onnxPath,
                               const QString &metricsPath, QString *error)
{
    if (name.isEmpty() || !QFileInfo::exists(onnxPath)) {
        if (error) {
            *error = QStringLiteral("模型文件无效");
        }
        return false;
    }
    QDir vroot(versionsRoot());
    vroot.mkpath(name);
    const QDir dst(vroot.filePath(name));
    const QString dstModel = dst.filePath(QStringLiteral("model.onnx"));
    QFile::remove(dstModel);
    if (!QFile::copy(onnxPath, dstModel)) {
        if (error) {
            *error = QStringLiteral("复制模型失败");
        }
        return false;
    }
    if (!metricsPath.isEmpty() && QFileInfo::exists(metricsPath)) {
        const QString dstMetrics = dst.filePath(QStringLiteral("metrics.json"));
        QFile::remove(dstMetrics);
        QFile::copy(metricsPath, dstMetrics);
    }
    // Carry the class list when it sits next to the produced model.
    const QString srcClasses = QFileInfo(onnxPath).dir().filePath(QStringLiteral("classes.txt"));
    if (QFileInfo::exists(srcClasses)) {
        const QString dstClasses = dst.filePath(QStringLiteral("classes.txt"));
        QFile::remove(dstClasses);
        QFile::copy(srcClasses, dstClasses);
    }
    return true;
}

QString ModelRegistry::suggestVersionName() const
{
    int maxN = 0;
    for (const ModelVersion &v : versions()) {
        const QRegularExpression re(QStringLiteral("yolo_v(\\d+)"));
        const QRegularExpressionMatch m = re.match(v.name);
        if (m.hasMatch()) {
            maxN = qMax(maxN, m.captured(1).toInt());
        }
    }
    return QStringLiteral("yolo_v%1").arg(maxN + 1);
}

} // namespace zp
