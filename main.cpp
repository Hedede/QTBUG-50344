// Reproducer for QTBUG-50344:
// QDateEdit / QDateTimeEdit with calendarPopup(true) and a style-sheeted
// drop-down button steps its value when clicked just left of the button,
// as if an invisible spin box up/down button were still there.
//
// Interactive:  ./repro         (click just left of the arrow, watch the log)
// Automatic:    ./repro --auto  (sweeps clicks across each edit; exit 1 = bug)

#include <QtWidgets>
#include <QtTest/QTest>

#ifndef REPRO_SOURCE_DIR
#define REPRO_SOURCE_DIR "."
#endif

// Style sheet from the bug report. %1 is the selector, %2 the image directory.
static const char styleSheetTemplate[] = R"(
%1{ margin-right: 20px;}
%1::drop-down{
  width: 16px;
  subcontrol-origin: margin;
  image: url(%2/custom_arrow.png);
  border-image: url(%2/custom_border.png) 2px 2px 2px 2px;
  border-width: 2px 2px 2px 2px;
}
)";

static QString styleSheetFor(const char *selector)
{
    return QString::fromLatin1(styleSheetTemplate)
            .arg(QLatin1String(selector), QStringLiteral(REPRO_SOURCE_DIR));
}

// The popup button, hit-tested the way QDateTimeEdit does it when calendarPopup is on.
static QRect dropDownRect(QDateTimeEdit *edit)
{
    QStyleOptionComboBox opt;
    opt.initFrom(edit);
    opt.editable = true;
    opt.frame = true;
    opt.subControls = QStyle::SC_ComboBoxArrow | QStyle::SC_ComboBoxEditField
                    | QStyle::SC_ComboBoxFrame;
    return edit->style()->subControlRect(QStyle::CC_ComboBox, &opt,
                                         QStyle::SC_ComboBoxArrow, edit);
}

// Where the spin box up/down buttons would be; these should not exist with calendarPopup.
static QRect spinButtonRect(QDateTimeEdit *edit, QStyle::SubControl sc)
{
    QStyleOptionSpinBox opt;
    opt.initFrom(edit);
    opt.frame = true;
    opt.buttonSymbols = edit->buttonSymbols();
    opt.subControls = QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown
                    | QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField;
    return edit->style()->subControlRect(QStyle::CC_SpinBox, &opt, sc, edit);
}

static QString rectString(const QRect &r)
{
    return QStringLiteral("x %1-%2, y %3-%4")
            .arg(r.left()).arg(r.right()).arg(r.top()).arg(r.bottom());
}

enum class Hit { None, Popup, StepUp, StepDown };

static const char *hitName(Hit hit)
{
    switch (hit) {
    case Hit::None:     return "nothing";
    case Hit::Popup:    return "popup";
    case Hit::StepUp:   return "STEP UP   <-- bug";
    case Hit::StepDown: return "STEP DOWN <-- bug";
    }
    return "?";
}

static Hit clickAt(QDateTimeEdit *edit, QPoint pos)
{
    const QDateTime before = edit->dateTime();

    // Approach from below the widget, as described in the report. Leaving the
    // widget also invalidates the cached hover rect, so every click is hit-tested afresh.
    QTest::mouseMove(edit, QPoint(pos.x(), edit->height() + 5));
    QTest::mouseMove(edit, pos);
    // Deliver to the child under the cursor (e.g. the line edit), like a real click.
    QWidget *target = edit->childAt(pos);
    if (!target)
        target = edit;
    QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, target->mapFrom(edit, pos));
    QCoreApplication::processEvents();

    Hit hit = Hit::None;
    QWidget *popup = edit->calendarWidget()->window();
    if (popup->isVisible()) {
        popup->hide();
        hit = Hit::Popup;
    }
    const QDateTime after = edit->dateTime();
    if (after > before)
        hit = Hit::StepUp;
    else if (after < before)
        hit = Hit::StepDown;

    edit->setDateTime(before);
    return hit;
}

// Clicks every pixel along a few horizontal lines and prints runs of identical results.
static bool sweep(const QString &name, QDateTimeEdit *edit)
{
    QTextStream out(stdout);
    out << "== " << name << " (" << edit->width() << "x" << edit->height() << ")\n"
        << "   drop-down rect:      " << rectString(dropDownRect(edit)) << "\n"
        << "   spin up rect:        " << rectString(spinButtonRect(edit, QStyle::SC_SpinBoxUp)) << "\n"
        << "   spin down rect:      " << rectString(spinButtonRect(edit, QStyle::SC_SpinBoxDown)) << "\n";

    bool stepped = false;
    const int h = edit->height();
    for (int y : { h / 4, h / 2, 3 * h / 4 }) {
        out << "   y=" << y << ":\n";
        int runStart = 0;
        Hit runHit = clickAt(edit, QPoint(0, y));
        for (int x = 1; x <= edit->width(); ++x) {
            const Hit hit = x < edit->width() ? clickAt(edit, QPoint(x, y)) : Hit::None;
            if (x < edit->width() && hit == runHit)
                continue;
            out << "      x " << runStart << "-" << x - 1 << ": " << hitName(runHit) << "\n";
            if (runHit == Hit::StepUp || runHit == Hit::StepDown)
                stepped = true;
            runStart = x;
            runHit = hit;
        }
    }
    out << "   => " << (stepped ? "BUG REPRODUCED" : "ok") << "\n\n";
    out.flush();
    return stepped;
}

// Logs mouse presses and value changes so the bug is visible when clicking by hand.
// Each value change is attributed to its cause; only a step caused by holding the
// mouse down outside the drop-down button is the bug. Picking a date in the
// calendar, keyboard and wheel edits are legitimate.
class ClickLogger : public QObject
{
public:
    explicit ClickLogger(QPlainTextEdit *log, QObject *parent = nullptr)
        : QObject(parent), m_log(log) {}

    void watch(const QString &name, QDateTimeEdit *edit)
    {
        edit->setObjectName(name);
        edit->installEventFilter(this);
        // Presses in the text field go to the line edit, never to the spin box itself.
        if (QLineEdit *lineEdit = edit->findChild<QLineEdit *>())
            lineEdit->installEventFilter(this);

        connect(edit, &QDateTimeEdit::dateTimeChanged, this, [this, edit](const QDateTime &dt) {
            m_log->appendPlainText(QStringLiteral("%1: value changed to %2  [%3]")
                                   .arg(edit->objectName(), dt.toString(Qt::ISODate),
                                        QLatin1String(changeCause(edit))));
        });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonPress)
            return QObject::eventFilter(watched, event);

        auto *edit = qobject_cast<QDateTimeEdit *>(watched);
        if (!edit) {
            // Press in the line edit: legitimate cursor placement / section selection.
            m_pressOutsideDropDown.remove(watched->parent());
            return QObject::eventFilter(watched, event);
        }

        auto *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = me->position().toPoint();
#else
        const QPoint pos = me->pos();
#endif
        const QRect dropDown = dropDownRect(edit);
        const bool outside = !dropDown.contains(pos);
        m_pressOutsideDropDown[edit] = outside;
        m_log->appendPlainText(QStringLiteral("%1: press at (%2,%3), %4 drop-down %5")
                               .arg(edit->objectName()).arg(pos.x()).arg(pos.y())
                               .arg(QLatin1String(outside ? "outside" : "on"), rectString(dropDown)));
        return QObject::eventFilter(watched, event);
    }

private:
    const char *changeCause(QDateTimeEdit *edit) const
    {
        // QCalendarPopup emits the picked date before it closes.
        if (edit->calendarWidget()->window()->isVisible())
            return "picked in calendar, ok";
        // Still held: covers the first step and the auto-repeat steps after it.
        const bool held = QGuiApplication::mouseButtons() & Qt::LeftButton;
        if (held && m_pressOutsideDropDown.value(edit, false))
            return "BUG: stepped by a click outside the drop-down";
        return "keyboard/wheel, ok";
    }

    QPlainTextEdit *m_log;
    QHash<QObject *, bool> m_pressOutsideDropDown;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const bool autoMode = app.arguments().contains(QStringLiteral("--auto"));
    const QDateTime start(QDate(2016, 1, 15), QTime(12, 30));

    auto *styledDate = new QDateEdit(start.date());
    styledDate->setCalendarPopup(true);
    styledDate->setStyleSheet(styleSheetFor("QDateEdit"));

    auto *styledDateTime = new QDateTimeEdit(start);
    styledDateTime->setCalendarPopup(true);
    styledDateTime->setStyleSheet(styleSheetFor("QDateTimeEdit"));

    auto *plainDate = new QDateEdit(start.date());
    plainDate->setCalendarPopup(true);

    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);

    QWidget window;
    window.setWindowTitle(QStringLiteral("QTBUG-50344 reproducer (Qt %1)").arg(QLatin1String(qVersion())));
    auto *form = new QFormLayout(&window);
    form->setVerticalSpacing(20);
    form->addRow(new QLabel(QStringLiteral(
        "Click just LEFT of the drop-down arrow (approach from below).\n"
        "With calendarPopup=true the value must never step on a click.")));
    form->addRow(QStringLiteral("Styled QDateEdit:"), styledDate);
    form->addRow(QStringLiteral("Styled QDateTimeEdit:"), styledDateTime);
    form->addRow(QStringLiteral("Unstyled QDateEdit (control):"), plainDate);
    form->addRow(log);

    ClickLogger logger(log);
    logger.watch(QStringLiteral("styled QDateEdit"), styledDate);
    logger.watch(QStringLiteral("styled QDateTimeEdit"), styledDateTime);
    logger.watch(QStringLiteral("unstyled QDateEdit"), plainDate);

    window.resize(420, 400);
    window.show();

    if (!autoMode)
        return app.exec();

    if (!QTest::qWaitForWindowExposed(&window))
        qWarning("window was not exposed");

    QTextStream(stdout) << "Qt " << qVersion() << ", platform " << QGuiApplication::platformName()
                        << ", style " << app.style()->objectName() << "\n\n";
    bool bug = false;
    bug |= sweep(QStringLiteral("styled QDateEdit"), styledDate);
    bug |= sweep(QStringLiteral("styled QDateTimeEdit"), styledDateTime);
    bug |= sweep(QStringLiteral("unstyled QDateEdit (control)"), plainDate);
    return bug ? 1 : 0;
}
