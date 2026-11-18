#include "analysis/evidence.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace zp {

namespace {

QString hms(double seconds)
{
    if (seconds < 0) {
        seconds = 0;
    }
    const int total = int(seconds + 0.5);
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString htmlEscape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

} // namespace

bool EvidenceExporter::writeReport(const QString &mediaName,
                                   const QVector<EvidenceEvent> &events,
                                   const QString &outDir, QString *error)
{
    QDir dir(outDir);
    if (!dir.exists() && !QDir().mkpath(outDir)) {
        if (error) *error = QStringLiteral("无法创建目录：%1").arg(outDir);
        return false;
    }

    // ---- copy thumbnails into the evidence folder ----
    QVector<QString> thumbs(events.size());
    for (int i = 0; i < events.size(); ++i) {
        const QString &src = events.at(i).thumbSource;
        if (src.isEmpty() || !QFileInfo::exists(src)) {
            continue;
        }
        const QString name = QStringLiteral("event_%1.jpg").arg(i + 1, 3, 10, QLatin1Char('0'));
        const QString dst = dir.filePath(name);
        QFile::remove(dst);
        if (QFile::copy(src, dst)) {
            thumbs[i] = name;
        }
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    // ---- report.json ----
    QJsonObject root;
    root.insert(QStringLiteral("media"), mediaName);
    root.insert(QStringLiteral("generatedAt"), stamp);
    root.insert(QStringLiteral("eventCount"), events.size());
    QJsonArray arr;
    for (int i = 0; i < events.size(); ++i) {
        const EvidenceEvent &e = events.at(i);
        QJsonObject o;
        o.insert(QStringLiteral("start"), e.start);
        o.insert(QStringLiteral("end"), e.end);
        o.insert(QStringLiteral("startText"), hms(e.start));
        o.insert(QStringLiteral("endText"), hms(e.end));
        o.insert(QStringLiteral("label"), e.label);
        o.insert(QStringLiteral("classId"), e.classId);
        o.insert(QStringLiteral("peakCount"), e.peakCount);
        if (!thumbs[i].isEmpty()) {
            o.insert(QStringLiteral("thumbnail"), thumbs[i]);
        }
        arr.append(o);
    }
    root.insert(QStringLiteral("events"), arr);

    QFile json(dir.filePath(QStringLiteral("report.json")));
    if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("无法写入 report.json");
        return false;
    }
    json.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    json.close();

    // ---- report.html ----
    QFile html(dir.filePath(QStringLiteral("report.html")));
    if (!html.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入 report.html");
        return false;
    }
    QTextStream ts(&html);
    ts.setCodec("UTF-8");
    ts << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
          "<title>证据清单</title><style>"
          "body{font-family:'Microsoft YaHei',sans-serif;background:#0d1117;color:#c9d1d9;"
          "margin:24px;}h1{font-size:20px;color:#2f81f7;}"
          "table{border-collapse:collapse;width:100%;margin-top:16px;}"
          "th,td{border:1px solid #30363d;padding:8px;text-align:left;vertical-align:top;}"
          "th{background:#161b22;}img{max-width:220px;border-radius:4px;}"
          ".meta{color:#8b949e;font-size:13px;}"
          "</style></head><body>";
    ts << "<h1>证据清单</h1>";
    ts << "<div class=\"meta\">源文件：" << htmlEscape(mediaName)
       << " &nbsp;|&nbsp; 生成时间：" << htmlEscape(stamp)
       << " &nbsp;|&nbsp; 事件数：" << events.size() << "</div>";
    ts << "<table><tr><th>#</th><th>时间段</th><th>目标</th><th>峰值数量</th><th>截图</th></tr>";
    for (int i = 0; i < events.size(); ++i) {
        const EvidenceEvent &e = events.at(i);
        ts << "<tr><td>" << (i + 1) << "</td><td>" << hms(e.start) << " – " << hms(e.end)
           << "</td><td>" << htmlEscape(e.label) << "</td><td>" << e.peakCount
           << "</td><td>";
        if (!thumbs[i].isEmpty()) {
            ts << "<img src=\"" << thumbs[i] << "\" alt=\"event\">";
        }
        ts << "</td></tr>";
    }
    ts << "</table></body></html>";
    html.close();

    return true;
}

} // namespace zp
