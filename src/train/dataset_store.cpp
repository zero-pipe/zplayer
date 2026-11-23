#include "train/dataset_store.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

namespace zp {

namespace {

QJsonObject boxToJson(const AnalysisBox &b)
{
    QJsonObject o;
    o.insert(QStringLiteral("cls"), b.classId);
    o.insert(QStringLiteral("label"), b.label);
    o.insert(QStringLiteral("score"), double(b.score));
    o.insert(QStringLiteral("x"), b.rect.x());
    o.insert(QStringLiteral("y"), b.rect.y());
    o.insert(QStringLiteral("w"), b.rect.width());
    o.insert(QStringLiteral("h"), b.rect.height());
    return o;
}

AnalysisBox boxFromJson(const QJsonObject &o)
{
    AnalysisBox b;
    b.classId = o.value(QStringLiteral("cls")).toInt(-1);
    b.label = o.value(QStringLiteral("label")).toString();
    b.score = float(o.value(QStringLiteral("score")).toDouble());
    b.rect = QRectF(o.value(QStringLiteral("x")).toDouble(),
                    o.value(QStringLiteral("y")).toDouble(),
                    o.value(QStringLiteral("w")).toDouble(),
                    o.value(QStringLiteral("h")).toDouble());
    return b;
}

// Built-in classes keep their COCO ids so samples captured before the class
// table existed (0=人, 2=车, 16=狗) keep their meaning.
QVector<ClassDef> defaultClasses()
{
    return {
        {0, QStringLiteral("person"), QString::fromUtf8("人")},
        {2, QStringLiteral("car"), QString::fromUtf8("车")},
        {16, QStringLiteral("dog"), QString::fromUtf8("狗")},
    };
}

} // namespace

DatasetStore &DatasetStore::instance()
{
    static DatasetStore s;
    return s;
}

DatasetStore::DatasetStore()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_root = QDir(base).filePath(QStringLiteral("dataset"));
    // Dev/CI hook: point the dataset at a throwaway directory for screenshots
    // and smoke tests without touching the user's real samples.
    const QString envRoot = qEnvironmentVariable("ZP_DATASET_DIR");
    if (!envRoot.trimmed().isEmpty()) {
        m_root = envRoot;
    } else {
        const QString saved = QSettings().value(QStringLiteral("ai/datasetRoot")).toString();
        if (!saved.trimmed().isEmpty()) {
            m_root = saved;
        }
    }
    ensureDirs();
    refreshCount();
}

void DatasetStore::setRoot(const QString &root)
{
    if (root.trimmed().isEmpty()) {
        return;
    }
    m_root = root;
    QSettings().setValue(QStringLiteral("ai/datasetRoot"), root);
    ensureDirs();
    refreshCount();
}

QString DatasetStore::framesDir() const { return QDir(m_root).filePath(QStringLiteral("frames")); }
QString DatasetStore::labelsDir() const { return QDir(m_root).filePath(QStringLiteral("labels")); }
QString DatasetStore::manifestPath() const { return QDir(m_root).filePath(QStringLiteral("manifest.jsonl")); }
QString DatasetStore::classesPath() const { return QDir(m_root).filePath(QStringLiteral("classes.json")); }

QVector<ClassDef> DatasetStore::classes() const
{
    QFile f(classesPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return defaultClasses();
    }
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    QVector<ClassDef> out;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ClassDef c;
        c.id = o.value(QStringLiteral("id")).toInt(-1);
        c.name = o.value(QStringLiteral("name")).toString();
        c.zh = o.value(QStringLiteral("zh")).toString();
        if (c.id >= 0 && (!c.name.isEmpty() || !c.zh.isEmpty())) {
            out.append(c);
        }
    }
    if (out.isEmpty()) {
        return defaultClasses();
    }
    std::sort(out.begin(), out.end(), [](const ClassDef &a, const ClassDef &b) {
        return a.id < b.id;
    });
    return out;
}

void DatasetStore::setClasses(const QVector<ClassDef> &classes)
{
    ensureDirs();
    QJsonArray arr;
    for (const ClassDef &c : classes) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), c.id);
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("zh"), c.zh);
        arr.append(o);
    }
    QFile f(classesPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}

QString DatasetStore::className(int id) const
{
    for (const ClassDef &c : classes()) {
        if (c.id == id) {
            return c.zh.isEmpty() ? c.name : c.zh;
        }
    }
    return id >= 0 ? QString::number(id) : QString();
}

int DatasetStore::classIndex(int id) const
{
    const QVector<ClassDef> cls = classes();
    for (int i = 0; i < cls.size(); ++i) {
        if (cls[i].id == id) {
            return i;
        }
    }
    return -1;
}

int DatasetStore::addClass(const QString &name, const QString &zh, QString *error)
{
    const QString ascii = name.trimmed();
    const QString display = zh.trimmed();
    if (ascii.isEmpty() && display.isEmpty()) {
        if (error) {
            *error = QStringLiteral("类别名不能为空");
        }
        return -1;
    }
    QVector<ClassDef> cls = classes();
    for (const ClassDef &c : cls) {
        if (!display.isEmpty() && c.zh.compare(display, Qt::CaseInsensitive) == 0) {
            if (error) {
                *error = QStringLiteral("类别已存在");
            }
            return -1;
        }
        if (!ascii.isEmpty() && c.name.compare(ascii, Qt::CaseInsensitive) == 0) {
            if (error) {
                *error = QStringLiteral("类别已存在");
            }
            return -1;
        }
    }
    int nextId = 80;   // stay clear of the COCO id range
    for (const ClassDef &c : cls) {
        nextId = qMax(nextId, c.id + 1);
    }
    ClassDef c;
    c.id = nextId;
    c.name = ascii.isEmpty() ? QStringLiteral("class_%1").arg(nextId) : ascii;
    c.zh = display;
    cls.append(c);
    setClasses(cls);
    return c.id;
}

void DatasetStore::ensureDirs() const
{
    QDir().mkpath(framesDir());
    QDir().mkpath(labelsDir());
}

QString DatasetStore::sanitize(const QString &s) const
{
    QString out = s.isEmpty() ? QStringLiteral("cam") : s;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) {
            out[i] = QLatin1Char('_');
        }
    }
    return out;
}

QString DatasetStore::newFramePath(const QString &channel) const
{
    ensureDirs();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString q = QStringLiteral("%1_%2.jpg").arg(sanitize(channel), stamp);
    return QDir(framesDir()).filePath(q);
}

SampleRecord DatasetStore::add(const QString &framePath, const QString &channel, double pts,
                               const QString &reason, const QVector<AnalysisBox> &boxes)
{
    ensureDirs();
    SampleRecord rec;
    rec.id = QFileInfo(framePath).completeBaseName();
    rec.framePath = framePath;
    rec.labelPath = QDir(labelsDir()).filePath(rec.id + QStringLiteral(".txt"));
    rec.channel = channel;
    rec.reason = reason;
    rec.pts = pts;
    rec.done = false;
    rec.boxes = boxes;

    QJsonObject o;
    o.insert(QStringLiteral("id"), rec.id);
    o.insert(QStringLiteral("frame"), rec.framePath);
    o.insert(QStringLiteral("channel"), rec.channel);
    o.insert(QStringLiteral("reason"), rec.reason);
    o.insert(QStringLiteral("pts"), rec.pts);
    o.insert(QStringLiteral("done"), rec.done);
    QJsonArray arr;
    for (const AnalysisBox &b : boxes) {
        arr.append(boxToJson(b));
    }
    o.insert(QStringLiteral("boxes"), arr);

    QFile f(manifestPath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        ts << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)) << '\n';
        f.close();
        ++m_count;
    }
    return rec;
}

QVector<SampleRecord> DatasetStore::load() const
{
    QVector<SampleRecord> out;
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QJsonObject o = QJsonDocument::fromJson(line.toUtf8()).object();
        SampleRecord rec;
        rec.id = o.value(QStringLiteral("id")).toString();
        rec.framePath = o.value(QStringLiteral("frame")).toString();
        rec.labelPath = QDir(labelsDir()).filePath(rec.id + QStringLiteral(".txt"));
        rec.channel = o.value(QStringLiteral("channel")).toString();
        rec.reason = o.value(QStringLiteral("reason")).toString();
        rec.pts = o.value(QStringLiteral("pts")).toDouble();
        rec.done = o.value(QStringLiteral("done")).toBool();
        const QJsonArray arr = o.value(QStringLiteral("boxes")).toArray();
        for (const QJsonValue &v : arr) {
            rec.boxes.append(boxFromJson(v.toObject()));
        }
        out.append(rec);
    }
    return out;
}

void DatasetStore::rewrite(const QVector<SampleRecord> &records) const
{
    ensureDirs();
    QFile f(manifestPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    for (const SampleRecord &rec : records) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), rec.id);
        o.insert(QStringLiteral("frame"), rec.framePath);
        o.insert(QStringLiteral("channel"), rec.channel);
        o.insert(QStringLiteral("reason"), rec.reason);
        o.insert(QStringLiteral("pts"), rec.pts);
        o.insert(QStringLiteral("done"), rec.done);
        QJsonArray arr;
        for (const AnalysisBox &b : rec.boxes) {
            arr.append(boxToJson(b));
        }
        o.insert(QStringLiteral("boxes"), arr);
        ts << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)) << '\n';
    }
    f.close();
    m_count = records.size();
}

bool DatasetStore::saveLabels(const SampleRecord &rec, const QVector<AnalysisBox> &boxes) const
{
    ensureDirs();
    QFile f(rec.labelPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream ts(&f);
    for (const AnalysisBox &b : boxes) {
        const int idx = classIndex(b.classId);
        if (idx < 0) {
            continue;   // not in the project class table -> not a valid label
        }
        const double cx = b.rect.x() + b.rect.width() / 2.0;
        const double cy = b.rect.y() + b.rect.height() / 2.0;
        ts << idx << ' ' << QString::number(cx, 'f', 6) << ' '
           << QString::number(cy, 'f', 6) << ' '
           << QString::number(b.rect.width(), 'f', 6) << ' '
           << QString::number(b.rect.height(), 'f', 6) << '\n';
    }
    return true;
}

bool DatasetStore::exportYolo(const QString &outDir, double valRatio, QString *error) const
{
    const QVector<SampleRecord> recs = load();
    if (recs.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有可导出的样本");
        }
        return false;
    }

    // Group records by channel; assign whole channels to val to avoid leakage.
    QStringList channels;
    for (const SampleRecord &r : recs) {
        if (!channels.contains(r.channel)) {
            channels.append(r.channel);
        }
    }
    channels.sort();
    QSet<QString> valChannels;
    const int valCount = qMax(1, int(channels.size() * qBound(0.0, valRatio, 0.5) + 0.5));
    for (int i = 0; i < valCount && i < channels.size(); ++i) {
        valChannels.insert(channels.at(i));
    }

    const QDir rootDir(outDir);
    if (!QDir().mkpath(outDir)) {
        if (error) {
            *error = QStringLiteral("无法创建导出目录");
        }
        return false;
    }
    QDir().mkpath(rootDir.filePath(QStringLiteral("images/train")));
    QDir().mkpath(rootDir.filePath(QStringLiteral("images/val")));
    QDir().mkpath(rootDir.filePath(QStringLiteral("labels/train")));
    QDir().mkpath(rootDir.filePath(QStringLiteral("labels/val")));

    // Project classes -> contiguous YOLO indices (0..N-1). Custom classes
    // (安全帽 etc.) ride along the same mapping, so retraining needs no extra
    // COCO assumptions.
    const QVector<ClassDef> cls = classes();
    QHash<int, int> idxOf;
    for (int i = 0; i < cls.size(); ++i) {
        idxOf.insert(cls[i].id, i);
    }

    int nTrain = 0;
    int nVal = 0;
    int skippedBoxes = 0;
    for (const SampleRecord &r : recs) {
        if (r.boxes.isEmpty() || !QFileInfo::exists(r.framePath)) {
            continue;
        }
        const bool isVal = valChannels.contains(r.channel);
        const QString imgDir = isVal ? QStringLiteral("images/val") : QStringLiteral("images/train");
        const QString lblDir = isVal ? QStringLiteral("labels/val") : QStringLiteral("labels/train");
        const QString imgName = QFileInfo(r.framePath).fileName();
        const QString imgDst = rootDir.filePath(imgDir + QLatin1Char('/') + imgName);
        if (!QFileInfo::exists(imgDst)) {
            QFile::copy(r.framePath, imgDst);
        }
        QFile lf(rootDir.filePath(lblDir + QLatin1Char('/') + QFileInfo(imgName).completeBaseName()
                                  + QStringLiteral(".txt")));
        if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream ts(&lf);
            for (const AnalysisBox &b : r.boxes) {
                const int idx = idxOf.value(b.classId, -1);
                if (idx < 0) {
                    ++skippedBoxes;
                    continue;
                }
                const double cx = b.rect.x() + b.rect.width() / 2.0;
                const double cy = b.rect.y() + b.rect.height() / 2.0;
                ts << idx << ' ' << QString::number(cx, 'f', 6) << ' '
                   << QString::number(cy, 'f', 6) << ' '
                   << QString::number(b.rect.width(), 'f', 6) << ' '
                   << QString::number(b.rect.height(), 'f', 6) << '\n';
            }
        }
        if (isVal) {
            ++nVal;
        } else {
            ++nTrain;
        }
    }

    if (nTrain == 0) {
        if (error) {
            *error = QStringLiteral("训练集为空");
        }
        return false;
    }

    // data.yaml uses ASCII names (stable for ultralytics); classes.txt carries
    // the display names (Chinese for custom classes) for the detector UI.
    const int nc = cls.size();
    QFile yaml(rootDir.filePath(QStringLiteral("data.yaml")));
    if (!yaml.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法写 data.yaml");
        }
        return false;
    }
    {
        QTextStream ts(&yaml);
        ts.setCodec("UTF-8");
        ts << "path: " << QDir(outDir).absolutePath().replace('\\', '/') << '\n';
        ts << "train: images/train\nval: images/val\n";
        ts << "nc: " << nc << '\n';
        ts << "names:\n";
        for (int i = 0; i < nc; ++i) {
            QString name = cls[i].name;
            if (name.isEmpty()) {
                name = QStringLiteral("class_%1").arg(i);
            }
            ts << "  " << i << ": " << name << '\n';
        }
    }
    yaml.close();

    QFile clsFile(rootDir.filePath(QStringLiteral("classes.txt")));
    if (clsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream ts(&clsFile);
        ts.setCodec("UTF-8");
        for (int i = 0; i < nc; ++i) {
            ts << (cls[i].zh.isEmpty() ? cls[i].name : cls[i].zh) << '\n';
        }
    }
    clsFile.close();

    if (skippedBoxes > 0) {
        qWarning().noquote()
            << QStringLiteral("[dataset] %1 个标注框的类别不在类别表中，已跳过").arg(skippedBoxes);
    }
    return true;
}

int DatasetStore::count() const { return m_count; }

void DatasetStore::refreshCount()
{
    m_count = 0;
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        if (!ts.readLine().trimmed().isEmpty()) {
            ++m_count;
        }
    }
}

void DatasetStore::clear()
{
    QDir fd(framesDir());
    for (const QString &file : fd.entryList(QDir::Files)) {
        fd.remove(file);
    }
    QDir ld(labelsDir());
    for (const QString &file : ld.entryList(QDir::Files)) {
        ld.remove(file);
    }
    QFile::remove(manifestPath());
    m_count = 0;
}

} // namespace zp
