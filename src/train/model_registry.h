#ifndef ZP_MODEL_REGISTRY_H
#define ZP_MODEL_REGISTRY_H

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace zp {

struct ModelVersion {
    QString name;          // folder name, e.g. yolo_v3
    QString dir;           // absolute version dir
    QString modelPath;     // <dir>/model.onnx
    QString metricsPath;   // <dir>/metrics.json (may be empty)
    QJsonObject metrics;   // parsed metrics (may be empty)
    bool hasMetrics = false;
};

// Local model versions under <appdir>/models/versions/<name>/, with a
// <appdir>/models/current.json pointer. The detector honours current.json so
// publishing / rollback take effect on the next model load.
class ModelRegistry {
public:
    static ModelRegistry &instance();

    QString root() const;          // <appdir>/models
    QString versionsRoot() const;  // root/versions

    QVector<ModelVersion> versions() const;
    QString currentVersion() const;
    bool setCurrent(const QString &name);

    // Register a produced ONNX as a new version (copies the file in).
    bool addVersion(const QString &name, const QString &onnxPath,
                    const QString &metricsPath, QString *error);
    QString suggestVersionName() const;

private:
    ModelRegistry();

    QString currentJsonPath() const;
    QString m_root;
};

} // namespace zp

#endif // ZP_MODEL_REGISTRY_H
