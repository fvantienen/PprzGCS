#include "pfd.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <math.h>
#include <AircraftManager.h>
#include "dispatcher_ui.h"

// colors of the indicator (the same in both themes: it is an instrument)
static const QColor SKY_TOP(0x2f7fd1), SKY_BOTTOM(0x7cb8ee);
static const QColor GROUND_TOP(0x9a6a3a), GROUND_BOTTOM(0x6b4423);
static const QColor SYMBOL(0xffc400);

Pfd::Pfd(QWidget *parent) : QWidget(parent), current_ac("")
{
    connect(DispatcherUi::get(), &DispatcherUi::ac_selected, this, &Pfd::changeCurrentAC);
    connect(PprzDispatcher::get(), &PprzDispatcher::flight_param, this, &Pfd::updateEulers);
    setToolTip("Attitude (roll, pitch) and heading of the selected aircraft");
}

void Pfd::paintEvent(QPaintEvent *event) {
    (void)event;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    bool valid = eulers.find(current_ac) != eulers.end();
    Eulers e = valid ? eulers[current_ac] : Eulers{0, 0, 0};
    QColor ac_color = QColor(0x808080);
    if(valid && AircraftManager::get()->aircraftExists(current_ac)) {
        ac_color = AircraftManager::get()->getAircraft(current_ac)->getColor();
    }

    // square attitude indicator, the heading tape below
    const double tape_h = 34, values_h = 22;
    double side = qMin<double>(width(), height() - tape_h - values_h - 12);
    QRectF att((width() - side) / 2, 0, side, side);
    QRectF tape(att.left(), att.bottom() + 6, side, tape_h);
    QRectF values(att.left(), tape.bottom() + 6, side, values_h);

    paintAttitude(p, att, e.roll, e.pitch, ac_color, valid);
    paintHeading(p, tape, e.yaw, valid);

    // the values as text below the instrument
    QFont vf = font();
    vf.setPixelSize(13);
    p.setFont(vf);
    QColor text = palette().color(QPalette::WindowText);
    QColor secondary = text;
    secondary.setAlpha(170);
    auto value = [&](QRectF r, QString caption, QString v) {
        p.setPen(secondary);
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, caption);
        QFont bf = vf;
        bf.setBold(true);
        p.setFont(bf);
        p.setPen(text);
        p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, v);
        p.setFont(vf);
    };
    QString dash = QString::fromUtf8("\xE2\x80\x94");
    double third = values.width() / 3;
    value(QRectF(values.left(), values.top(), third - 10, values_h), "Roll", valid ? QString("%1°").arg(e.roll, 0, 'f', 0) : dash);
    value(QRectF(values.left() + third + 5, values.top(), third - 10, values_h), "Pitch", valid ? QString("%1°").arg(e.pitch, 0, 'f', 0) : dash);
    double hd = fmod(fmod(e.yaw, 360.) + 360., 360.);
    value(QRectF(values.left() + 2 * third + 10, values.top(), third - 10, values_h), "Hdg", valid ? QString("%1°").arg(static_cast<int>(round(hd)) % 360) : dash);
}

void Pfd::paintAttitude(QPainter& p, QRectF r, float roll, float pitch, QColor ac_color, bool valid) {
    const double radius = 10;
    const double px_per_deg = r.height() / 60.;    // +-30 deg visible
    QPointF c = r.center();

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p.setClipPath(clip);

    // horizon: rotated by the roll, moved down by the pitch
    p.save();
    p.translate(c);
    p.rotate(-roll);
    p.translate(0, pitch * px_per_deg);
    double big = r.width() * 2;
    QLinearGradient sky(0, -big / 2, 0, 0);
    sky.setColorAt(0, SKY_TOP);
    sky.setColorAt(1, SKY_BOTTOM);
    QLinearGradient ground(0, 0, 0, big / 2);
    ground.setColorAt(0, GROUND_TOP);
    ground.setColorAt(1, GROUND_BOTTOM);
    p.setPen(Qt::NoPen);
    p.setBrush(sky);
    p.drawRect(QRectF(-big, -big, 2 * big, big));
    p.setBrush(ground);
    p.drawRect(QRectF(-big, 0, 2 * big, big));
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(QPointF(-big, 0), QPointF(big, 0));

    // pitch ladder every 5 deg, labels every 10
    QFont f = font();
    f.setPixelSize(qMax(9, static_cast<int>(r.height() / 22)));
    f.setBold(true);
    p.setFont(f);
    for(int deg = -60; deg <= 60; deg += 5) {
        if(deg == 0) {
            continue;
        }
        double y = -deg * px_per_deg;
        double w = (deg % 10 == 0) ? r.width() * 0.22 : r.width() * 0.10;
        p.setPen(QPen(Qt::white, deg % 10 == 0 ? 2 : 1.4));
        p.drawLine(QPointF(-w / 2, y), QPointF(w / 2, y));
        if(deg % 10 == 0) {
            QString txt = QString::number(abs(deg));
            QRectF lr(w / 2 + 4, y - 8, 30, 16);
            p.drawText(lr, Qt::AlignVCenter | Qt::AlignLeft, txt);
            QRectF rr(-w / 2 - 34, y - 8, 30, 16);
            p.drawText(rr, Qt::AlignVCenter | Qt::AlignRight, txt);
        }
    }
    p.restore();

    // roll scale: ticks at the top, fixed; the pointer turns with the roll
    double rr = r.width() * 0.42;
    p.save();
    p.translate(c);
    p.setPen(QPen(Qt::white, 2));
    for(int a: {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60}) {
        p.save();
        p.rotate(a);
        double len = (a % 30 == 0) ? 12 : 7;
        p.drawLine(QPointF(0, -rr), QPointF(0, -rr - len));
        p.restore();
    }
    // the sky pointer, turning with the horizon
    p.rotate(-roll);
    QPolygonF pointer;
    pointer << QPointF(0, -rr + 1) << QPointF(-7, -rr + 12) << QPointF(7, -rr + 12);
    p.setPen(Qt::NoPen);
    p.setBrush(SYMBOL);
    p.drawPolygon(pointer);
    p.restore();

    // fixed aircraft symbol, in the aircraft color outline
    p.save();
    p.translate(c);
    double s = r.width() * 0.18;
    QPainterPath wings;
    wings.moveTo(-s * 1.6, 0);
    wings.lineTo(-s * 0.55, 0);
    wings.lineTo(-s * 0.3, s * 0.3);
    wings.moveTo(s * 1.6, 0);
    wings.lineTo(s * 0.55, 0);
    wings.lineTo(s * 0.3, s * 0.3);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(wings);
    p.setPen(QPen(SYMBOL, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(wings);
    p.setPen(QPen(Qt::black, 1.5));
    p.setBrush(SYMBOL);
    p.drawEllipse(QPointF(0, 0), 4, 4);
    p.restore();

    // values: roll and pitch as text
    QFont vf = font();
    vf.setPixelSize(qMax(10, static_cast<int>(r.height() / 18)));
    vf.setBold(true);
    p.setFont(vf);
    auto chip = [&](QRectF cr, QString txt) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(cr, 5, 5);
        p.setPen(Qt::white);
        p.drawText(cr, Qt::AlignCenter, txt);
    };
    double ch = vf.pixelSize() + 10;
    if(!valid) {
        chip(QRectF(c.x() - r.width() * 0.3, r.bottom() - ch - 8, r.width() * 0.6, ch), "No attitude data");
    }
    p.restore();

    // border in the aircraft color
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ac_color, 3));
    p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
}

void Pfd::paintHeading(QPainter& p, QRectF r, float heading, bool valid) {
    const double radius = 8;
    const double px_per_deg = r.width() / 90.;    // 90 deg visible
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p.setClipPath(clip);
    p.fillRect(r, QColor(0x1b1f25));

    QFont f = font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    double hd = fmod(fmod(heading, 360.) + 360., 360.);
    double cx = r.center().x();
    for(int d = static_cast<int>(hd) - 60; d <= static_cast<int>(hd) + 60; d++) {
        if(d % 5 != 0) {
            continue;
        }
        double x = cx + (d - hd) * px_per_deg;
        int dn = ((d % 360) + 360) % 360;
        p.setPen(QPen(QColor(0xd6dce3), 1.4));
        double len = (dn % 10 == 0) ? 8 : 4;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.top() + len));
        if(dn % 30 == 0) {
            QString txt;
            switch(dn) {
            case 0: txt = "N"; break;
            case 90: txt = "E"; break;
            case 180: txt = "S"; break;
            case 270: txt = "W"; break;
            default: txt = QString::number(dn / 10);
            }
            p.drawText(QRectF(x - 15, r.top() + 10, 30, r.height() - 12), Qt::AlignHCenter | Qt::AlignVCenter, txt);
        }
    }
    p.restore();

    // the heading in a box in the middle
    QRectF box(cx - 28, r.top() + 4, 56, r.height() - 8);
    p.setPen(QPen(SYMBOL, 2));
    p.setBrush(QColor(0x1b1f25));
    p.drawRoundedRect(box, 5, 5);
    p.setPen(Qt::white);
    QFont bf = font();
    bf.setPixelSize(14);
    bf.setBold(true);
    p.setFont(bf);
    p.drawText(box, Qt::AlignCenter, valid ? QString("%1°").arg(static_cast<int>(round(hd)) % 360, 3, 10, QChar('0')) : "---");
}

void Pfd::changeCurrentAC(QString id) {
    if(AircraftManager::get()->aircraftExists(id)) {
        current_ac = id;
        update();
    }
}

void Pfd::updateEulers(pprzlink::Message msg) {
    QString ac_id;
    float roll, pitch, heading;
    msg.getField("ac_id", ac_id);
    if(AircraftManager::get()->aircraftExists(ac_id)) {
        msg.getField("roll", roll);
        msg.getField("pitch", pitch);
        msg.getField("heading", heading);
        eulers[ac_id] = Eulers {roll, pitch, heading};
        if(ac_id == current_ac) {
            update();
        }
    }
}

QSize Pfd::sizeHint() const
{
    return QSize(260, 320);
}

QSize Pfd::minimumSizeHint() const
{
    return QSize(200, 260);
}
