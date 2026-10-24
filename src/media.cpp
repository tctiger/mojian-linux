#include "media.h"
#include <QtSvg/QSvgRenderer>
#include <cmath>

namespace Mojian {
static QString group(const QString &text, int &index, QChar open = '{', QChar close = '}') {
    while (index < text.size() && text[index].isSpace()) ++index;
    if (index >= text.size() || text[index] != open) return {};
    ++index; int depth = 1; QString result;
    while (index < text.size()) {
        QChar c = text[index++];
        if (c == '\\' && index < text.size()) { result += c; result += text[index++]; continue; }
        if (c == open) ++depth;
        if (c == close && --depth == 0) return result;
        if (c == close) { /* depth already decremented */ }
        result += c;
    }
    return {};
}
QString expandMath(const QString &input, const QString &preamble, bool *ok) {
    if (ok) *ok = false;
    if (input.size() + preamble.size() > 16000) return {};
    struct Macro { int count; QString body; }; QMap<QString, Macro> macros;
    QString source = preamble + "\n" + input, output;
    int i = 0;
    while (i < source.size()) {
        if (source[i] == '%') { while (i < source.size() && source[i] != '\n') ++i; continue; }
        if (source.mid(i, 11) == "\\newcommand" || source.mid(i, 13) == "\\renewcommand") {
            i += source.mid(i, 11) == "\\newcommand" ? 11 : 13;
            QString name = group(source, i); if (!QRegularExpression("^\\\\[A-Za-z]+$").match(name).hasMatch() || macros.size() >= 100) return {};
            while (i < source.size() && source[i].isSpace()) ++i;
            int count = 0; if (i < source.size() && source[i] == '[') { bool valid; count = group(source, i, '[', ']').toInt(&valid); if (!valid || count < 0 || count > 3) return {}; }
            QString body = group(source, i); if (body.isNull()) return {}; macros[name.mid(1)] = {count, body};
        } else output += source[i++];
    }
    source = output;
    for (int pass = 0; pass < 8; ++pass) {
        output.clear(); bool changed = false; i = 0;
        while (i < source.size()) {
            if (source[i] != '\\') { output += source[i++]; continue; }
            int start = i++; QString name; while (i < source.size() && source[i].isLetter()) name += source[i++];
            if (!macros.contains(name)) { output += source.mid(start, i - start); if (name.isEmpty() && i < source.size()) output += source[i++]; continue; }
            auto macro = macros[name]; QString value = macro.body;
            QStringList arguments;
            for (int j = 0; j < macro.count; ++j) {
                while (i < source.size() && source[i].isSpace()) ++i;
                if (i >= source.size()) return {};
                arguments << (source[i] == '{' ? group(source, i) : QString(source[i++]));
            }
            for (int j = arguments.size() - 1; j >= 0; --j) value.replace("#" + QString::number(j + 1), arguments[j]);
            output += value; changed = true; if (output.size() > 16000) return {};
        }
        source = output; if (!changed) { if (ok) *ok = true; return source.trimmed(); }
    }
    return {};
}
struct MathBox { QImage image; qreal baseline = 0; };
static QImage canvas(qreal width, qreal height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width > 12000 || height > 12000 || width * height > 16000000) return {};
    QImage image(QSize(std::max(1, int(std::ceil(width))), std::max(1, int(std::ceil(height)))), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); return image;
}
static MathBox rowBoxes(const QList<MathBox> &boxes) {
    qreal baseline = 0, bottom = 0, width = 0;
    for (const auto &box : boxes) { baseline = std::max(baseline, box.baseline); bottom = std::max(bottom, box.image.height() - box.baseline); width += box.image.width(); }
    MathBox result{canvas(width, baseline + bottom), baseline}; if (result.image.isNull()) return result;
    QPainter painter(&result.image); qreal x = 0; for (const auto &box : boxes) { painter.drawImage(QPointF(x, baseline - box.baseline), box.image); x += box.image.width(); } return result;
}
class MathParser {
public:
    QString source; int index = 0, depth = 0; bool supported = true, roman = false, bold = false; QString family = "serif"; QColor color;
    MathParser(QString text, QColor tint) : source(std::move(text)), color(tint) {}
    MathBox text(QString value, qreal size) {
        QFont font(family); font.setPointSizeF(size); font.setItalic(!roman); font.setBold(bold);
        QFontMetricsF metrics(font); MathBox box{canvas(metrics.horizontalAdvance(value) + 2, metrics.height() + 3), metrics.ascent() + 1};
        if (!box.image.isNull()) { QPainter painter(&box.image); painter.setRenderHint(QPainter::TextAntialiasing); painter.setFont(font); painter.setPen(color); painter.drawText(QPointF(1, box.baseline), value); } return box;
    }
    MathBox arg(qreal size) { while (index < source.size() && source[index].isSpace()) ++index; if (index < source.size() && source[index] == '{') { ++index; return sequence(size, true); } return atom(size); }
    MathBox atom(qreal size) {
        if (index >= source.size()) return text("", size);
        QChar c = source[index++]; if (c == '{') return sequence(size, true); if (c != '\\') return text(QString(c), size);
        QString name; while (index < source.size() && source[index].isLetter()) name += source[index++];
        if (name.isEmpty()) return text(index < source.size() ? QString(source[index++]) : "\\", size);
        if (QStringList{"frac", "dfrac", "tfrac", "binom"}.contains(name)) {
            auto top = arg(size * .85), bottom = arg(size * .85); qreal width = std::max(top.image.width(), bottom.image.width()) + 12;
            MathBox box{canvas(width, top.image.height() + bottom.image.height() + 10), top.image.height() + 5 + size * .2};
            if (!box.image.isNull()) { QPainter painter(&box.image); painter.setPen(color); painter.drawImage(QPointF((width - top.image.width()) / 2, 0), top.image); if (name != "binom") painter.drawLine(QPointF(2, top.image.height() + 4), QPointF(width - 2, top.image.height() + 4)); painter.drawImage(QPointF((width - bottom.image.width()) / 2, top.image.height() + 9), bottom.image); }
            if (name == "binom") { auto left = text("(", size * 1.4), right = text(")", size * 1.4); left.image = left.image.scaled(left.image.width(), box.image.height()); right.image = right.image.scaled(right.image.width(), box.image.height()); left.baseline = right.baseline = box.baseline; return rowBoxes({left, box, right}); } return box;
        }
        if (name == "sqrt") {
            MathBox degree; while (index < source.size() && source[index].isSpace()) ++index; if (index < source.size() && source[index] == '[') { QString raw = group(source, index, '[', ']'); MathParser parser(raw, color); degree = parser.sequence(size * .5); }
            auto body = arg(size), root = text("√", std::max(size, qreal(body.image.height()) * .75));
            MathBox box{canvas(root.image.width() + body.image.width() + 5, body.image.height() + 6), body.baseline + 5};
            if (!box.image.isNull()) { QPainter p(&box.image); p.setPen(color); p.drawImage(0, 2, root.image); p.drawImage(root.image.width(), 5, body.image); p.drawImage(0, 0, degree.image); p.drawLine(root.image.width() - 1, 3, box.image.width() - 1, 3); } return box;
        }
        if (QStringList{"text", "mathrm", "mathbf", "mathit", "mathsf", "mathtt", "operatorname", "mathbb", "ce"}.contains(name)) {
            bool oldRoman = roman, oldBold = bold; QString oldFamily = family; roman = name != "mathit"; bold = name == "mathbf"; family = name == "mathtt" ? "monospace" : name == "mathsf" ? "sans-serif" : "serif";
            MathBox box;
            if (name == "mathbb" || name == "ce") {
                QString raw = group(source, index);
                if (name == "mathbb") { const QMap<QChar, QString> map{{'R', "ℝ"}, {'N', "ℕ"}, {'Z', "ℤ"}, {'Q', "ℚ"}, {'C', "ℂ"}, {'H', "ℍ"}, {'P', "ℙ"}}; QString value; for (auto ch : raw) value += map.value(ch, QString(ch)); box = text(value, size); }
                else { raw.replace("<=>", "\\rightleftharpoons ").replace("->", "\\rightarrow ").replace("<-", "\\leftarrow "); raw.replace(QRegularExpression("([A-Za-z)\\]])(\\d+)"), "\\1_{\\2}"); MathParser parser(raw, color); parser.roman = true; box = parser.sequence(size); supported &= parser.supported; }
            } else box = arg(size);
            roman = oldRoman; bold = oldBold; family = oldFamily; return box;
        }
        if (QStringList{"overline", "underline", "bar", "vec", "hat", "dot", "ddot"}.contains(name)) {
            auto body = arg(size); bool bottom = name == "underline"; MathBox box{canvas(body.image.width() + 4, body.image.height() + 8), body.baseline + (bottom ? 0 : 8)};
            if (!box.image.isNull()) { QPainter p(&box.image); p.setPen(QPen(color, 1)); p.setBrush(color); p.drawImage(2, bottom ? 0 : 8, body.image); qreal y = bottom ? body.image.height() + 3 : 4, w = body.image.width();
                if (name == "dot" || name == "ddot") { p.drawEllipse(QPointF(w / 2, y), 1.3, 1.3); if (name == "ddot") p.drawEllipse(QPointF(w / 2 + 5, y), 1.3, 1.3); }
                else if (name == "hat") p.drawPolyline(QPolygonF{QPointF(2, y + 2), QPointF(w / 2, y - 2), QPointF(w + 2, y + 2)});
                else { p.drawLine(QPointF(2, y), QPointF(w + 2, y)); if (name == "vec") p.drawPolyline(QPolygonF{QPointF(w - 2, y - 3), QPointF(w + 2, y), QPointF(w - 2, y + 3)}); }
            } return box;
        }
        if (name == "quad" || name == "qquad") return text(name == "quad" ? "  " : "    ", size);
        if (QStringList{"left", "right", "displaystyle", "textstyle", "limits", "nolimits"}.contains(name)) return text("", size);
        static const QMap<QString, QString> symbols = [] {
            QMap<QString, QString> map; QStringList keys = QString("alpha beta gamma delta epsilon theta lambda mu pi rho sigma tau phi omega Gamma Delta Sigma Omega sum prod int infty partial nabla times cdot div pm leq geq neq approx equiv rightarrow leftarrow rightleftharpoons leftrightarrow subset subseteq supset supseteq emptyset ell hbar langle rangle lbrace rbrace iint iiint oint to mapsto Rightarrow in notin forall exists cup cap sin cos tan log ln lim ldots").split(' ');
            QStringList values = QString("α β γ δ ε θ λ μ π ρ σ τ φ ω Γ Δ Σ Ω ∑ ∏ ∫ ∞ ∂ ∇ × · ÷ ± ≤ ≥ ≠ ≈ ≡ → ← ⇌ ↔ ⊂ ⊆ ⊃ ⊇ ∅ ℓ ℏ ⟨ ⟩ { } ∬ ∭ ∮ → ↦ ⇒ ∈ ∉ ∀ ∃ ∪ ∩ sin cos tan log ln lim …").split(' ');
            for (int i = 0; i < keys.size(); ++i) map[keys[i]] = values.value(i); return map;
        }();
        if (symbols.contains(name)) return text(symbols[name], size);
        supported = false; return text("\\" + name, size);
    }
    MathBox sequence(qreal size, bool untilBrace = false) {
        if (++depth > 64) { supported = false; --depth; return {}; }
        QList<MathBox> boxes;
        while (index < source.size()) {
            if (source[index] == '}' && untilBrace) { ++index; break; }
            auto base = atom(size); MathBox upper, lower;
            while (index < source.size() && (source[index] == '^' || source[index] == '_')) { auto marker = source[index++]; auto script = arg(size * .65); if (marker == '^') upper = script; else lower = script; }
            if (!upper.image.isNull() || !lower.image.isNull()) {
                int top = upper.image.height() * .65; MathBox box{canvas(base.image.width() + std::max(upper.image.width(), lower.image.width()) + 2, base.image.height() + top + lower.image.height() * .45), base.baseline + top};
                if (!box.image.isNull()) { QPainter p(&box.image); p.drawImage(0, top, base.image); p.drawImage(base.image.width(), 0, upper.image); p.drawImage(base.image.width(), top + base.baseline - 1, lower.image); } base = box;
            }
            boxes << base;
            if (boxes.size() > 8000) { supported = false; break; }
        }
        --depth; return rowBoxes(boxes);
    }
};
static QImage mathImageUncached(const QString &input, qreal size, QColor color) {
    bool ok; QString source = expandMath(input, QSettings().value("mathPreamble").toString(), &ok); if (!ok) return {};
    int balance = 0; bool escaped = false;
    for (auto c : source) { if (escaped) { escaped = false; continue; } if (c == '\\') { escaped = true; continue; } if (c == '{') ++balance; if (c == '}') --balance; if (balance < 0 || balance > 64) return {}; } if (balance) return {};
    if (source.contains("\\begin")) {
        auto m = QRegularExpression(R"(^\s*\\begin\{(matrix|pmatrix|bmatrix|vmatrix|aligned|align\*?|gather\*?|split|cases|array)\}(.*?)\\end\{\1\}\s*$)", QRegularExpression::DotMatchesEverythingOption).match(source);
        if (!m.hasMatch()) return {}; QString body = m.captured(2); if (m.captured(1) == "array") { int index = 0; group(body, index); body = body.mid(index); }
        QList<QList<MathBox>> rows; QList<qreal> widths; qreal height = 8;
        for (const auto &line : body.split("\\\\", Qt::SkipEmptyParts)) {
            QList<MathBox> cells; qreal h = 0; int c = 0;
            for (const auto &cell : line.split('&')) { MathParser p(cell, color); auto box = p.sequence(size); if (!p.supported) return {}; cells << box; while (widths.size() <= c) widths << 0; widths[c] = std::max(widths[c], qreal(box.image.width())); h = std::max(h, qreal(box.image.height())); ++c; } rows << cells; height += h + 8;
        }
        if (rows.size() > 100 || widths.size() > 100) return {}; qreal width = 24; for (auto w : widths) width += w + 12; QImage image = canvas(width, height); if (image.isNull()) return {};
        QPainter p(&image); p.setPen(color); qreal y = 4;
        for (const auto &row : rows) { qreal x = 14, h = 0; for (int c = 0; c < row.size(); ++c) { p.drawImage(QPointF(x, y), row[c].image); x += widths[c] + 12; h = std::max(h, qreal(row[c].image.height())); } y += h + 8; }
        QString kind = m.captured(1); p.setPen(QPen(color, 1.2));
        if (kind == "bmatrix" || kind == "vmatrix" || kind == "cases") { p.drawLine(5, 3, 5, image.height() - 3); if (kind != "cases") p.drawLine(image.width() - 5, 3, image.width() - 5, image.height() - 3); if (kind != "vmatrix") { p.drawLine(5, 3, 11, 3); p.drawLine(5, image.height() - 3, 11, image.height() - 3); } }
        if (kind == "pmatrix") { p.drawArc(QRectF(1, 0, 16, image.height()), 90 * 16, 180 * 16); p.drawArc(QRectF(image.width() - 17, 0, 16, image.height()), -90 * 16, 180 * 16); } return image;
    }
    MathParser parser(source, color); auto box = parser.sequence(size); if (!parser.supported || box.image.isNull()) return {};
    auto image = canvas(box.image.width() + 12, box.image.height() + 12); if (!image.isNull()) { QPainter painter(&image); painter.drawImage(6, 6, box.image); } return image;
}
static void arrow(QPainter &p, QPointF from, QPointF to, bool dashed = false) {
    auto old = p.pen(); auto pen = old; pen.setStyle(dashed ? Qt::DashLine : Qt::SolidLine); p.setPen(pen); p.drawLine(from, to); p.setPen(old);
    qreal angle = std::atan2(to.y() - from.y(), to.x() - from.x()); QPolygonF head{to, to - QPointF(std::cos(angle - .4) * 9, std::sin(angle - .4) * 9), to - QPointF(std::cos(angle + .4) * 9, std::sin(angle + .4) * 9)}; p.setBrush(old.color()); p.drawPolygon(head);
}
static QImage diagramImageUncached(const QString &source) {
    if (source.size() > 100000) return {};
    auto lines = source.split(QRegularExpression("[\\n;]"), Qt::SkipEmptyParts); if (lines.isEmpty()) return {}; QString kind = lines.first().trimmed();
    if (kind.startsWith("pie")) {
        QList<QPair<QString, double>> slices; QString title;
        for (const auto &line : lines) {
            if (line.trimmed().startsWith("title ")) title = line.trimmed().mid(6);
            auto match = QRegularExpression("^\\s*\"([^\"]+)\"\\s*:\\s*([\\d.]+)\\s*$").match(line);
            if (match.hasMatch()) { double value = match.captured(2).toDouble(); if (value > 0 && std::isfinite(value)) slices << qMakePair(match.captured(1), value); }
        }
        if (slices.isEmpty() || slices.size() > 100) return {}; auto image = canvas(680, std::max<qsizetype>(400, 50 + slices.size() * 24)); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.fillRect(image.rect(), Qt::white); p.setPen(Qt::black); p.drawText(QRect(20, 10, 620, 30), Qt::AlignCenter, title);
        double sum = 0; for (auto slice : slices) sum += slice.second; int start = 0;
        for (int i = 0; i < slices.size(); ++i) { QColor color = QColor::fromHsv((i * 47 + 210) % 360, 135, 215); p.setBrush(color); p.setPen(Qt::white); int span = qRound(slices[i].second / sum * 360 * 16); p.drawPie(QRect(25, 60, 320, 320), start, span); start += span; p.fillRect(QRect(380, 60 + i * 24, 14, 14), color); p.setPen(Qt::black); p.drawText(QPoint(405, 73 + i * 24), slices[i].first + QString(" (%1%)").arg(slices[i].second / sum * 100, 0, 'f', 1)); } return image;
    }
    if (kind.startsWith("sequenceDiagram")) {
        QStringList ids; QMap<QString, QString> labels; struct Message { QString from, to, label; bool dashed; }; QList<Message> messages;
        auto add = [&](QString id) { if (!ids.contains(id)) ids << id; };
        for (auto line : lines) {
            auto participant = QRegularExpression("^\\s*(?:participant|actor)\\s+(\\w+)(?:\\s+as\\s+(.+))?$").match(line);
            if (participant.hasMatch()) { add(participant.captured(1)); labels[participant.captured(1)] = participant.captured(2); }
            auto message = QRegularExpression("^\\s*(\\w+)\\s*(-{1,2}>>?)[+-]?\\s*(\\w+)\\s*:\\s*(.*)$").match(line);
            if (message.hasMatch()) { add(message.captured(1)); add(message.captured(3)); messages << Message{message.captured(1), message.captured(3), message.captured(4), message.captured(2).startsWith("--")}; }
        }
        if (ids.isEmpty() || ids.size() > 30 || messages.size() > 200) return {}; auto image = canvas(std::max<qsizetype>(400, ids.size() * 180), messages.size() * 64 + 130); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.fillRect(image.rect(), Qt::white);
        for (int i = 0; i < ids.size(); ++i) { p.setPen(QPen(QColor("#999999"), 1, Qt::DashLine)); p.drawLine(i * 180 + 90, 55, i * 180 + 90, image.height() - 15); p.setPen(Qt::black); p.setBrush(QColor("#eef1fa")); p.drawRoundedRect(QRect(i * 180 + 15, 10, 150, 40), 6, 6); p.drawText(QRect(i * 180 + 15, 10, 150, 40), Qt::AlignCenter, labels.value(ids[i]).isEmpty() ? ids[i] : labels[ids[i]]); }
        for (int i = 0; i < messages.size(); ++i) { auto m = messages[i]; int x1 = ids.indexOf(m.from) * 180 + 90, x2 = ids.indexOf(m.to) * 180 + 90, y = i * 64 + 100; p.setPen(QColor("#343a49")); if (x1 == x2) { p.drawPolyline(QPolygonF{QPointF(x1,y),QPointF(x1+45,y),QPointF(x1+45,y+25)}); arrow(p,{qreal(x1+45),qreal(y+25)},{qreal(x1),qreal(y+25)},m.dashed); } else arrow(p, {qreal(x1), qreal(y)}, {qreal(x2), qreal(y)}, m.dashed); p.drawText(QRect(std::min(x1,x2),y-25,std::max(150,std::abs(x2-x1)),24), Qt::AlignCenter, m.label); } return image;
    }
    if (kind.startsWith("gantt")) {
        struct Task { QString name, section, status; QDate start, end; bool milestone; }; QList<Task> tasks; QMap<QString,QDate> ends; QString title, section;
        QDate minimum, maximum;
        for (auto line : lines) {
            line = line.trimmed(); if (line.startsWith("title ")) { title = line.mid(6); continue; } if (line.startsWith("section ")) { section = line.mid(8); continue; }
            int colon = line.indexOf(':'); if (colon < 0) continue; QString name = line.left(colon).trimmed(); auto parts = line.mid(colon+1).split(',', Qt::SkipEmptyParts); for (auto &part : parts) part = part.trimmed();
            QString status, id; bool milestone = false;
            while (!parts.isEmpty() && QStringList{"done","active","crit","milestone"}.contains(parts.first())) { status = parts.takeFirst(); milestone |= status == "milestone"; }
            if (parts.size() >= 3 && !QDate::fromString(parts.first(),"yyyy-MM-dd").isValid() && !parts.first().startsWith("after ")) id = parts.takeFirst();
            if (parts.size() < 2) continue; QString startText = parts.takeFirst(); QDate start = startText.startsWith("after ") ? ends.value(startText.mid(6).trimmed()) : QDate::fromString(startText,"yyyy-MM-dd"); if (!start.isValid()) continue;
            QDate end = QDate::fromString(parts.first(),"yyyy-MM-dd"); if (!end.isValid()) { auto duration = QRegularExpression("^(\\d+)([dhw])$").match(parts.first()); if (!duration.hasMatch()) continue; int days = duration.captured(1).toInt(); if (duration.captured(2)=="w") days*=7; if (duration.captured(2)=="h") days=std::max(1,int(std::ceil(days/24.0))); end=start.addDays(days); }
            if (end<start) continue; tasks << Task{name,section,status,start,end,milestone}; if (!id.isEmpty()) ends[id]=end; if (!minimum.isValid()||start<minimum) minimum=start; if (!maximum.isValid()||end>maximum) maximum=end;
        }
        if (tasks.isEmpty() || tasks.size()>200) return {}; auto image=canvas(900,100+tasks.size()*36); QPainter p(&image); p.fillRect(image.rect(),Qt::white); p.setPen(Qt::black); p.drawText(QRect(10,5,880,30),Qt::AlignCenter,title); p.drawText(220,55,minimum.toString("yyyy-MM-dd")); p.drawText(760,55,maximum.toString("yyyy-MM-dd")); qreal days=std::max<qint64>(1,minimum.daysTo(maximum));
        for (int i=0;i<tasks.size();++i) { auto t=tasks[i]; int y=75+i*36; p.setPen(Qt::black); p.drawText(QRect(8,y,205,30),Qt::AlignVCenter,t.section.isEmpty()?t.name:t.section+" / "+t.name); QColor color=t.status=="done"?QColor("#72aa8a"):t.status=="crit"?QColor("#cc7780"):QColor("#7e91ce"); p.setBrush(color); p.setPen(Qt::NoPen); qreal x=220+minimum.daysTo(t.start)/days*650,w=std::max<qreal>(6,t.start.daysTo(t.end)/days*650); if(t.milestone)p.drawPolygon(QPolygonF{{x,y+4.},{x+8,y+14.},{x,y+24.},{x-8,y+14.}});else p.drawRoundedRect(QRectF(x,y+4,w,22),4,4); } return image;
    }
    bool classes=kind.startsWith("classDiagram"), states=kind.startsWith("stateDiagram");
    if (!(classes||states||kind.startsWith("flowchart")||kind.startsWith("graph"))) return {};
    struct Node { QString id,label,shape="box",color="#eef1fa"; }; struct Edge {QString a,b,label,style;}; QMap<QString,Node> nodes; QList<Edge> edges; QString rank="TB"; if (kind.contains(" LR"))rank="LR"; if(kind.contains(" RL"))rank="RL"; if(kind.contains(" BT"))rank="BT";
    QMap<QString,QStringList> groups;QMap<QString,QString> groupParents;QStringList groupStack;
    auto node=[&](QString raw) -> QString {
        raw=raw.trimmed(); if(raw=="[*]")raw="MOJIAN_START"; auto match=QRegularExpression("^([A-Za-z_][A-Za-z0-9_]*)(.*)$").match(raw); if(!match.hasMatch())return {};
        QString id=match.captured(1),tail=match.captured(2).trimmed(); if(!nodes.contains(id))nodes[id]={id,id};
        if(!tail.isEmpty()) { QString label=tail; label.remove(QRegularExpression("^[\\[({]+|[\\])}]+$")); if(label.startsWith('"')&&label.endsWith('"'))label=label.mid(1,label.size()-2); if(!label.isEmpty())nodes[id].label=label; if(tail.startsWith("{{"))nodes[id].shape="hexagon";else if(tail.startsWith('{'))nodes[id].shape="diamond";else if(tail.startsWith("(("))nodes[id].shape="circle";else if(tail.startsWith("[("))nodes[id].shape="cylinder";else if(tail.startsWith("[["))nodes[id].shape="box3d";else if(tail.startsWith("[/")||tail.startsWith("[\\"))nodes[id].shape="parallelogram";else if(tail.startsWith('('))nodes[id].shape="ellipse"; }
        if(!groupStack.isEmpty()&&!groups[groupStack.last()].contains(id))groups[groupStack.last()]<<id;
        if(id=="MOJIAN_START"){nodes[id].label="";nodes[id].shape="circle";} return id;
    };
    QString currentClass;
    for(int i=1;i<lines.size();++i){QString line=lines[i].trimmed();if(line.startsWith("%%")||line.isEmpty())continue;
        if(line.startsWith("subgraph ")){QString name=line.mid(9).trimmed();auto label=QRegularExpression("^\\w+\\s*\\[(.+)\\]$").match(name);if(label.hasMatch())name=label.captured(1);groupParents[name]=groupStack.isEmpty()?QString():groupStack.last();groups[name];groupStack<<name;continue;}if(line=="end"){if(!groupStack.isEmpty())groupStack.removeLast();continue;}
        if(classes&&line.startsWith("class ")){currentClass=node(line.mid(6).remove('{').trimmed());if(!line.contains('{'))currentClass.clear();continue;}
        if(line=="}"){currentClass.clear();continue;}if(!currentClass.isEmpty()){nodes[currentClass].label+="\n"+line;continue;}
        if(classes){auto member=QRegularExpression("^(\\w+)\\s*:\\s*(.+)$").match(line);if(member.hasMatch()){QString id=node(member.captured(1));nodes[id].label+="\n"+member.captured(2);continue;}}
        if(states){auto declaration=QRegularExpression("^state\\s+\"([^\"]+)\"\\s+as\\s+(\\w+)$").match(line);if(declaration.hasMatch()){QString id=node(declaration.captured(2));nodes[id].label=declaration.captured(1);continue;}}
        auto labelArrow=QRegularExpression("^(.+?)\\s+--\\s+([^>]+?)\\s+-->\\s+(.+)$").match(line);if(labelArrow.hasMatch())line=labelArrow.captured(1)+" -->|"+labelArrow.captured(2)+"| "+labelArrow.captured(3);
        auto style=QRegularExpression("^style\\s+(\\w+)\\s+.*?fill:(#[A-Fa-f0-9]{6})").match(line);if(style.hasMatch()){QString id=node(style.captured(1));nodes[id].color=style.captured(2);continue;}
        auto connector=QRegularExpression(R"((<\|--|--\|>|<\|\.\.|\.\.\|>|\.\.>|\.\.|<-->|-->|---|--\.|-\.->|==>|--|\*--|o--)(?:\|([^|]*)\|)?)");
        auto iterator=connector.globalMatch(line);int previous=0;QString left;while(iterator.hasNext()){auto m=iterator.next();QString a=left.isEmpty()?node(line.mid(previous,m.capturedStart()-previous)):left;int end=m.capturedEnd();auto following=connector.match(line,end);QString right=line.mid(end,following.hasMatch()?following.capturedStart()-end:-1);QString label=m.captured(2);int colon=right.indexOf(" : ");if(colon>=0){label=right.mid(colon+3);right=right.left(colon);}QString b=node(right);if(!a.isEmpty()&&!b.isEmpty())edges<<Edge{a,b,label,m.captured(1)};left=b;previous=end;}
        if(!line.contains("--")&&!line.contains("==>")){QString id=node(line);if(!id.isEmpty()&&!groupStack.isEmpty())groups[groupStack.last()]<<id;}
    }
    if(nodes.isEmpty()||nodes.size()>200||edges.size()>500)return {};
    auto quote=[](QString text){return "\""+text.replace('\\',"\\\\").replace('"',"\\\"").replace('\n',"\\n")+"\"";};
    QString dot="digraph G {rankdir="+rank+"; graph [bgcolor=white, pad=0.2]; node [fontname=\"sans-serif\",style=filled,color=\"#7b8aa6\"]; edge [fontname=\"sans-serif\",color=\"#6b7280\"];\n";
    for(auto n:nodes)dot+=quote(n.id)+" [label="+quote(n.label)+",shape="+n.shape+",fillcolor="+quote(n.color)+"];\n";
    QMap<QString,int> groupIds;int cluster=0;for(auto it=groups.cbegin();it!=groups.cend();++it)groupIds[it.key()]=cluster++;
    std::function<QString(const QString &,int)> groupDot=[&](const QString &name,int depth){if(depth>32)return QString();QString value="subgraph cluster_"+QString::number(groupIds[name])+" {label="+quote(name)+";";for(auto id:groups[name])value+=quote(id)+";";for(auto it=groupParents.cbegin();it!=groupParents.cend();++it)if(it.value()==name&&it.key()!=name)value+=groupDot(it.key(),depth+1);return value+"}\n";};for(auto it=groups.cbegin();it!=groups.cend();++it)if(groupParents.value(it.key()).isEmpty())dot+=groupDot(it.key(),0);
    for(auto e:edges){QString attributes="label="+quote(e.label);if(e.style=="---"||e.style=="--")attributes+=",arrowhead=none";if(e.style.contains('.'))attributes+=",style=dashed";if(e.style=="==>")attributes+=",penwidth=3";if(e.style=="<-->")attributes+=",dir=both";if(e.style.startsWith("<|"))attributes+=",dir=back,arrowtail=empty";else if(e.style.endsWith("|>"))attributes+=",arrowhead=empty";else if(e.style=="*--")attributes+=",dir=back,arrowtail=diamond";else if(e.style=="o--")attributes+=",dir=back,arrowtail=odiamond";dot+=quote(e.a)+" -> "+quote(e.b)+" ["+attributes+"];\n";}dot+="}";
    QString executable=QStandardPaths::findExecutable("dot");if(!executable.isEmpty()){QProcess process;process.start(executable,{"-Tsvg"});if(process.waitForStarted(1000)){process.write(dot.toUtf8());process.closeWriteChannel();if(process.waitForFinished(3000)&&process.exitCode()==0){QSvgRenderer svg(process.readAllStandardOutput());QSize size=svg.defaultSize();if(svg.isValid()&&size.width()>0&&size.height()>0&&size.width()<12000&&size.height()<12000){auto image=canvas(size.width(),size.height());if(!image.isNull()){QPainter p(&image);svg.render(&p);return image;}}}process.kill();process.waitForFinished(500);}}
    // Native fallback when Graphviz is unavailable.
    auto image=canvas(rank=="LR"?nodes.size()*190:420,rank=="LR"?180:nodes.size()*90);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.fillRect(image.rect(),Qt::white);QMap<QString,QRectF> rects;int index=0;
    for(auto n:nodes){QRectF r(rank=="LR"?index*190+15:100,rank=="LR"?50:index*90+15,160,55);rects[n.id]=r;++index;}
    p.setPen(QColor("#778099"));for(auto e:edges)arrow(p,rects[e.a].center(),rects[e.b].center(),e.style.contains('.'));
    for(auto n:nodes){auto r=rects[n.id];p.setBrush(QColor(n.color));if(n.shape=="diamond")p.drawPolygon(QPolygonF{{r.center().x(),r.top()},{r.right(),r.center().y()},{r.center().x(),r.bottom()},{r.left(),r.center().y()}});else if(n.shape=="circle"||n.shape=="ellipse")p.drawEllipse(r);else p.drawRoundedRect(r,5,5);p.setPen(Qt::black);p.drawText(r.adjusted(6,3,-6,-3),Qt::AlignCenter|Qt::TextWordWrap,n.label);p.setPen(QColor("#778099"));}return image;
}
QImage mathImage(const QString &source,qreal size,QColor color){static QCache<QString,QImage> cache(48000);QString key=QString::fromLatin1(QCryptographicHash::hash((source+QSettings().value("mathPreamble").toString()+QString::number(size)+QString::number(color.rgba())).toUtf8(),QCryptographicHash::Sha256).toHex());if(auto image=cache.object(key))return *image;auto image=mathImageUncached(source,size,color);cache.insert(key,new QImage(image),std::max(1,int(image.sizeInBytes()/1024)));return image;}
QImage diagramImage(const QString &source){static QCache<QString,QImage> cache(48000);QString key=QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex());if(auto image=cache.object(key))return *image;auto image=diagramImageUncached(source);cache.insert(key,new QImage(image),std::max(1,int(image.sizeInBytes()/1024)));return image;}
} // namespace Mojian
