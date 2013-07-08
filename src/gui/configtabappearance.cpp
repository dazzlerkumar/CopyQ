/*
    Copyright (c) 2013, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "gui/configtabappearance.h"
#include "ui_configtabappearance.h"

#include "common/client_server.h"
#include "common/option.h"
#include "gui/clipboardbrowser.h"
#include "item/clipboarditem.h"
#include "item/itemeditor.h"
#include "item/itemdelegate.h"

#include <QColorDialog>
#include <QFileDialog>
#include <QFontDialog>
#include <QMessageBox>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryFile>

#ifndef COPYQ_THEME_PREFIX
#   ifdef Q_OS_WIN
#       define COPYQ_THEME_PREFIX QApplication::applicationDirPath() + "/themes"
#   else
#       define COPYQ_THEME_PREFIX ""
#   endif
#endif

namespace {

QString getFontStyleSheet(const QString &fontString)
{
    QString result;
    if (fontString.isEmpty())
        return QString();

    QFont font;
    font.fromString(fontString);

    qreal size = font.pointSizeF();
    QString sizeUnits = "pt";
    if (size < 0.0) {
        size = font.pixelSize();
        sizeUnits = "px";
    }

    result.append( QString(";font-family: \"%1\"").arg(font.family()) );
    result.append( QString(";font:%1 %2 %3%4")
                   .arg(font.style() == QFont::StyleItalic
                        ? "italic" : font.style() == QFont::StyleOblique ? "oblique" : "normal")
                   .arg(font.bold() ? "bold" : "")
                   .arg(size)
                   .arg(sizeUnits)
                   );
    result.append( QString(";text-decoration:%1 %2 %3")
                   .arg(font.strikeOut() ? "line-through" : "")
                   .arg(font.underline() ? "underline" : "")
                   .arg(font.overline() ? "overline" : "")
                   );
    // QFont::weight -> CSS
    // (normal) 50 -> 400
    // (bold)   75 -> 700
    int w = font.weight() * 12 - 200;
    result.append( QString(";font-weight:%1").arg(w) );

    return result;
}

QString serializeColor(const QColor &color)
{
    if (color.alpha() == 255)
        return color.name();

    return QString("rgba(%1,%2,%3,%4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha() * 1.0 / 255);
}

QColor deserializeColor(const QString &colorName)
{
    if ( colorName.startsWith("rgba(") ) {
        QStringList list = colorName.mid(5, colorName.indexOf(')') - 5).split(',');
        int r = list.value(0).toInt();
        int g = list.value(1).toInt();
        int b = list.value(2).toInt();
        int a = list.value(3).toDouble() * 255;

        return QColor(r, g, b, a);
    }

    return QColor(colorName);
}

int normalizeColorValue(int value)
{
    return qBound(0, value, 255);
}

QColor evalColor(const QString &expression, const QHash<QString, Option> &theme, int maxRecursion = 8);

void addColor(const QString &color, float multiply, int *r, int *g, int *b, int *a,
              const QHash<QString, Option> &theme, int maxRecursion)
{
    if (color.isEmpty())
        return;

    QColor toAdd;
    float x = multiply;

    if (color.at(0).isDigit()) {
        bool ok;
        x = multiply * color.toFloat(&ok);
        if (!ok)
            return;
        toAdd = QColor(Qt::black);
    } else if ( color.startsWith('#') || color.startsWith("rgba(") ) {
        toAdd = deserializeColor(color);
    } else {
        if (maxRecursion > 0)
            toAdd = evalColor(theme.value(color).value().toString(), theme, maxRecursion - 1);
    }

    *r = normalizeColorValue(*r + x * toAdd.red());
    *g = normalizeColorValue(*g + x * toAdd.green());
    *b = normalizeColorValue(*b + x * toAdd.blue());
    if (multiply > 0.0)
        *a = normalizeColorValue(*a + x * toAdd.alpha());
}

QColor evalColor(const QString &expression, const QHash<QString, Option> &theme, int maxRecursion)
{
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;

    QStringList addList = QString(expression).remove(' ').split('+');
    foreach (const QString &add, addList) {
        QStringList subList = add.split('-');
        float multiply = 1;
        foreach (const QString &sub, subList) {
            addColor(sub, multiply, &r, &g, &b, &a, theme, maxRecursion);
            multiply = -1;
        }
    }

    return QColor(r, g, b, a);
}

} // namespace

ConfigTabAppearance::ConfigTabAppearance(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConfigTabAppearance)
    , m_theme()
    , m_editor()
{
    ui->setupUi(this);

    ClipboardBrowser *c = ui->clipboardBrowserPreview;
    c->addItems( QStringList()
                 << tr("Search string is \"item\".")
                 << tr("Select an item and\n"
                       "press F2 to edit.")
                 << tr("Select items and move them with\n"
                       "CTRL and up or down key.")
                 << tr("Remove item with Delete key.") );
    for (int i = 1; i <= 20; ++i)
        c->add( tr("Example item %1").arg(i), true, -1 );

    c->at(0)->setData( mimeItemNotes, tr("Some random notes (Shift+F2 to edit)").toUtf8() );
    c->filterItems( tr("item") );

    QAction *act = new QAction(c);
    act->setShortcut( QString("Shift+F2") );
    connect(act, SIGNAL(triggered()), c, SLOT(editNotes()));
    c->addAction(act);

    // Connect signals from theme buttons.
    foreach (QPushButton *button, ui->scrollAreaTheme->findChildren<QPushButton *>()) {
        if (button->objectName().endsWith("Font"))
            connect(button, SIGNAL(clicked()), SLOT(onFontButtonClicked()));
        else if (button->objectName().startsWith("pushButtonColor"))
            connect(button, SIGNAL(clicked()), SLOT(onColorButtonClicked()));
    }

    initThemeOptions();
}

void ConfigTabAppearance::decorateBrowser(ClipboardBrowser *c) const
{
    QFont font;
    QPalette p;
    QColor color;

    // scrollbars
    Qt::ScrollBarPolicy scrollbarPolicy = themeValue("show_scrollbars").toBool()
            ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
    c->setVerticalScrollBarPolicy(scrollbarPolicy);
    c->setHorizontalScrollBarPolicy(scrollbarPolicy);

    // colors and font
    c->setStyleSheet(
        QString("ClipboardBrowser,#item{")
        + getFontStyleSheet( themeValue("font").toString() )
        + ";color:" + themeColorString("fg")
        + ";background:" + themeColorString("bg")
        + "}"

        + QString("ClipboardBrowser::item:alternate{")
        + ";color:" + themeColorString("alt_fg")
        + ";background:" + themeColorString("alt_bg")
        + "}"

        + QString("ClipboardBrowser::item:selected,#item[CopyQ_selected=\"true\"]{")
        + ";color:" + themeColorString("sel_fg")
        + ";background:" + themeColorString("sel_bg")
        + "}"

        + QString("#item{background:transparent}")
        + QString("#item[CopyQ_selected=\"true\"]{background:transparent}")

        + getToolTipStyleSheet()

        // Allow user to change CSS.
        + QString("ClipboardBrowser{") + themeStyleSheet("item_css") + "}"
        + QString("ClipboardBrowser::item:alternate{") + themeStyleSheet("alt_item_css") + "}"
        + QString("ClipboardBrowser::item:selected{") + themeStyleSheet("sel_item_css") + "}"
        + themeStyleSheet("css")
        );

    // search style
    ItemDelegate *d = static_cast<ItemDelegate *>( c->itemDelegate() );
    font.fromString( themeValue("find_font").toString() );
    color = themeColor("find_bg");
    p.setColor(QPalette::Base, color);
    color = themeColor("find_fg");
    p.setColor(QPalette::Text, color);
    d->setSearchStyle(font, p);

    // editor style
    d->setSearchStyle(font, p);
    font.fromString( themeValue("edit_font").toString() );
    color = themeColor("edit_bg");
    p.setColor(QPalette::Base, color);
    color = themeColor("edit_fg");
    p.setColor(QPalette::Text, color);
    d->setEditorStyle(font, p);

    // number style
    d->setShowNumber(themeValue("show_number").toBool());
    font.fromString( themeValue("num_font").toString() );
    color = themeColor("num_fg");
    p.setColor(QPalette::Text, color);
    d->setNumberStyle(font, p);

    c->redraw();
}

void ConfigTabAppearance::decorateTabs(QWidget *tabWidget) const
{
    tabWidget->setStyleSheet(
        QString("#tab_tree{") + themeStyleSheet("tab_tree_css") + "}"
        + QString("#tab_tree::item{") + themeStyleSheet("tab_tree_item_css") + "}"
        + QString("#tab_tree::branch:selected, #tab_tree::item:selected{")
                + themeStyleSheet("tab_tree_sel_item_css") + "}"
        );
}

QString ConfigTabAppearance::getToolTipStyleSheet() const
{
    return QString("QToolTip{")
            + getFontStyleSheet( themeValue("notes_font").toString() )
            + ";background:" + themeColorString("notes_bg")
            + ";color:" + themeColorString("notes_fg")
            + ";" + themeStyleSheet("notes_css")
            + "}";
}

void ConfigTabAppearance::showEvent(QShowEvent *event)
{
    updateThemes();
    ui->scrollAreaTheme->setMinimumWidth( ui->scrollAreaThemeContents->minimumSizeHint().width()
                                          + ui->scrollAreaTheme->verticalScrollBar()->width() + 8);
    QWidget::showEvent(event);
}

ConfigTabAppearance::~ConfigTabAppearance()
{
    delete ui;
}

void ConfigTabAppearance::loadTheme(QSettings &settings)
{
    updateTheme(settings, &m_theme);

    updateColorButtons();
    updateFontButtons();

    decorateBrowser(ui->clipboardBrowserPreview);
}

void ConfigTabAppearance::saveTheme(QSettings &settings) const
{
    QStringList keys = m_theme.keys();
    keys.sort();

    foreach (const QString &key, keys)
        settings.setValue( key, themeValue(key) );
}

QVariant ConfigTabAppearance::themeValue(const QString &name) const
{
    return themeValue(name, m_theme);
}

void ConfigTabAppearance::onFontButtonClicked()
{
    Q_ASSERT(sender() != NULL);
    fontButtonClicked(sender());
}

void ConfigTabAppearance::onColorButtonClicked()
{
    Q_ASSERT(sender() != NULL);
    colorButtonClicked(sender());
}

void ConfigTabAppearance::on_pushButtonLoadTheme_clicked()
{
    const QString filename = QFileDialog::getOpenFileName(this, tr("Open Theme File"),
                                                          defaultUserThemePath(), QString("*.ini"));
    if ( !filename.isNull() ) {
        QSettings settings(filename, QSettings::IniFormat);
        loadTheme(settings);
    }

    updateThemes();
}

void ConfigTabAppearance::on_pushButtonSaveTheme_clicked()
{
    QString filename = QFileDialog::getSaveFileName(this, tr("Save Theme File As"),
                                                    defaultUserThemePath(), QString("*.ini"));
    if ( !filename.isNull() ) {
        if ( !filename.endsWith(".ini") )
            filename.append(".ini");
        QSettings settings(filename, QSettings::IniFormat);
        saveTheme(settings);
    }

    updateThemes();
}

void ConfigTabAppearance::on_pushButtonResetTheme_clicked()
{
    initThemeOptions();
    updateColorButtons();
    updateFontButtons();
    decorateBrowser(ui->clipboardBrowserPreview);
}

void ConfigTabAppearance::on_pushButtonEditTheme_clicked()
{
    if (m_editor.isEmpty()) {
        QMessageBox::warning( this, tr("No External Editor"),
                              tr("Set external editor command first!") );
        return;
    }

    const QString tmpFileName = QString("CopyQ.XXXXXX.ini");
    QString tmpPath = QDir( QDir::tempPath() ).absoluteFilePath(tmpFileName);

    QTemporaryFile tmpfile;
    tmpfile.setFileTemplate(tmpPath);
    tmpfile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    if ( tmpfile.open() ) {
        {
            QSettings settings(tmpfile.fileName(), QSettings::IniFormat);
            saveTheme(settings);
            settings.sync();
        }

        QByteArray data = tmpfile.readAll();
        // keep ini file user friendly
        data.replace("\\n",
#ifdef Q_OS_WIN
                     "\r\n"
#else
                     "\n"
#endif
                     );

        ItemEditor *editor = new ItemEditor(data, "application/x-copyq-theme", m_editor, this);

        connect( editor, SIGNAL(fileModified(QByteArray,QString)),
                 this, SLOT(onThemeModified(QByteArray)) );

        connect( editor, SIGNAL(closed(QObject *)),
                 editor, SLOT(deleteLater()) );

        if ( !editor->start() )
            delete editor;
    }
}

void ConfigTabAppearance::on_checkBoxShowNumber_stateChanged(int)
{
    decorateBrowser(ui->clipboardBrowserPreview);
}

void ConfigTabAppearance::on_checkBoxScrollbars_stateChanged(int)
{
    decorateBrowser(ui->clipboardBrowserPreview);
}

void ConfigTabAppearance::on_comboBoxThemes_activated(const QString &text)
{
    if ( text.isEmpty() )
        return;

    QString fileName = defaultUserThemePath() + "/" + text + ".ini";
    if ( !QFile(fileName).exists() ) {
        fileName = COPYQ_THEME_PREFIX;
        if ( fileName.isEmpty() || !QFile(fileName).exists() )
            return;
        fileName.append("/" + text + ".ini");
    }

    QSettings settings(fileName, QSettings::IniFormat);
    loadTheme(settings);
}

void ConfigTabAppearance::onThemeModified(const QByteArray &bytes)
{
    const QString tmpFileName = QString("CopyQ.XXXXXX.ini");
    QString tmpPath = QDir( QDir::tempPath() ).absoluteFilePath(tmpFileName);

    QTemporaryFile tmpfile;
    tmpfile.setFileTemplate(tmpPath);
    tmpfile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    if ( !tmpfile.open() )
        return;

    tmpfile.write(bytes);
    tmpfile.flush();

    QSettings settings(tmpfile.fileName(), QSettings::IniFormat);
    loadTheme(settings);
}

void ConfigTabAppearance::updateTheme(QSettings &settings, QHash<QString, Option> *theme)
{
    foreach ( const QString &key, theme->keys() ) {
        if ( settings.contains(key) ) {
            QVariant value = settings.value(key);
            if ( value.isValid() )
                (*theme)[key].setValue(value);
        }
    }
}

void ConfigTabAppearance::updateThemes()
{
    // Add themes in combo box.
    ui->comboBoxThemes->clear();
    ui->comboBoxThemes->addItem(QString());

    const QStringList nameFilters("*.ini");
    const QDir::Filters filters = QDir::Files | QDir::Readable;

    QDir themesDir( defaultUserThemePath() );
    if ( themesDir.mkpath(".") ) {
        foreach ( const QFileInfo &fileInfo,
                  themesDir.entryInfoList(nameFilters, filters, QDir::Name) )
        {
            const QIcon icon = createThemeIcon( themesDir.absoluteFilePath(fileInfo.fileName()) );
            ui->comboBoxThemes->addItem( icon, fileInfo.baseName() );
        }
    }

    const QString themesPath(COPYQ_THEME_PREFIX);
    if ( !themesPath.isEmpty() ) {
        QDir dir(themesPath);
        foreach ( const QFileInfo &fileInfo,
                  dir.entryList(nameFilters, filters, QDir::Name) )
        {
            const QString name = fileInfo.baseName();
            if ( ui->comboBoxThemes->findText(name) == -1 ) {
                const QIcon icon = createThemeIcon( dir.absoluteFilePath(fileInfo.fileName()) );
                ui->comboBoxThemes->addItem(icon, name);
            }
        }
    }
}

void ConfigTabAppearance::fontButtonClicked(QObject *button)
{
    QFont font;
    font.fromString( button->property("VALUE").toString() );
    QFontDialog dialog(font, this);
    if ( dialog.exec() == QDialog::Accepted ) {
        font = dialog.selectedFont();
        button->setProperty( "VALUE", font.toString() );
        decorateBrowser(ui->clipboardBrowserPreview);

        updateFontButtons();
    }
}

void ConfigTabAppearance::colorButtonClicked(QObject *button)
{
    QColor color = evalColor( button->property("VALUE").toString(), m_theme );
    QColorDialog dialog(this);
    dialog.setOptions(dialog.options() | QColorDialog::ShowAlphaChannel);
    dialog.setCurrentColor(color);

    if ( dialog.exec() == QDialog::Accepted ) {
        color = dialog.selectedColor();
        button->setProperty( "VALUE", serializeColor(color) );
        decorateBrowser(ui->clipboardBrowserPreview);

        QPixmap pix(16, 16);
        pix.fill(color);
        button->setProperty("icon", QIcon(pix));

        updateFontButtons();
    }
}

void ConfigTabAppearance::updateColorButtons()
{
    /* color indicating icons for color buttons */
    QSize iconSize(16, 16);
    QPixmap pix(iconSize);

    QList<QPushButton *> buttons =
            ui->scrollAreaTheme->findChildren<QPushButton *>(QRegExp("^pushButtonColor"));

    foreach (QPushButton *button, buttons) {
        QColor color = evalColor( button->property("VALUE").toString(), m_theme );
        pix.fill(color);
        button->setIcon(pix);
        button->setIconSize(iconSize);
    }
}

void ConfigTabAppearance::updateFontButtons()
{
    QSize iconSize(32, 16);
    QPixmap pix(iconSize);

    QRegExp re("^pushButton(.*)Font$");
    QList<QPushButton *> buttons = ui->scrollAreaTheme->findChildren<QPushButton *>(re);

    foreach (QPushButton *button, buttons) {
        re.indexIn(button->objectName());
        const QString colorButtonName = "pushButtonColor" + re.cap(1);

        QPushButton *buttonFg = ui->scrollAreaTheme->findChild<QPushButton *>(colorButtonName + "Fg");
        QColor colorFg = (buttonFg == NULL) ? themeColor("fg")
                                            : evalColor( buttonFg->property("VALUE").toString(), m_theme );

        QPushButton *buttonBg = ui->scrollAreaTheme->findChild<QPushButton *>(colorButtonName + "Bg");
        QColor colorBg = (buttonBg == NULL) ? themeColor("bg")
                                            : evalColor( buttonBg->property("VALUE").toString(), m_theme );

        pix.fill((colorBg.alpha() < 255) ? themeColor("bg") : colorBg);

        QPainter painter(&pix);
        painter.setPen(colorFg);

        QFont font;
        font.fromString( button->property("VALUE").toString() );
        painter.setFont(font);
        painter.drawText( QRect(0, 0, iconSize.width(), iconSize.height()), Qt::AlignCenter, tr("Abc") );

        button->setIcon(pix);
        button->setIconSize(iconSize);
    }
}

QColor ConfigTabAppearance::themeColor(const QString &name) const
{
    return themeColor(name, m_theme);
}

QString ConfigTabAppearance::themeColorString(const QString &name) const
{
    return serializeColor( themeColor(name) );
}

QString ConfigTabAppearance::themeStyleSheet(const QString &name) const
{
    QString css = themeValue(name).toString();
    int i = 0;

    forever {
        i = css.indexOf("${", i);
        if (i == -1)
            break;
        int j = css.indexOf('}', i + 2);
        if (j == -1)
            break;

        const QString var = css.mid(i + 2, j - i - 2);

        const QString colorName = serializeColor( evalColor(var, m_theme) );
        css.replace(i, j - i + 1, colorName);
        i += colorName.size();
    }

    return css;
}

void ConfigTabAppearance::initThemeOptions()
{
    QString name;
    QPalette p;
    name = serializeColor( p.color(QPalette::Base) );
    m_theme["bg"]          = Option(name, "VALUE", ui->pushButtonColorBg);
    m_theme["edit_bg"]     = Option(name, "VALUE", ui->pushButtonColorEditorBg);
    name = serializeColor( p.color(QPalette::Text) );
    m_theme["fg"]          = Option(name, "VALUE", ui->pushButtonColorFg);
    m_theme["edit_fg"]     = Option(name, "VALUE", ui->pushButtonColorEditorFg);
    name = serializeColor( p.color(QPalette::Text).lighter(400) );
    m_theme["num_fg"]      = Option(name, "VALUE", ui->pushButtonColorNumberFg);
    name = serializeColor( p.color(QPalette::AlternateBase) );
    m_theme["alt_bg"]      = Option(name, "VALUE", ui->pushButtonColorAltBg);
    name = serializeColor( p.color(QPalette::Highlight) );
    m_theme["sel_bg"]      = Option(name, "VALUE", ui->pushButtonColorSelBg);
    name = serializeColor( p.color(QPalette::HighlightedText) );
    m_theme["sel_fg"]      = Option(name, "VALUE", ui->pushButtonColorSelFg);
    m_theme["find_bg"]     = Option("#ff0", "VALUE", ui->pushButtonColorFoundBg);
    m_theme["find_fg"]     = Option("#000", "VALUE", ui->pushButtonColorFoundFg);
    name = serializeColor( p.color(QPalette::ToolTipBase) );
    m_theme["notes_bg"]  = Option(name, "VALUE", ui->pushButtonColorNotesBg);
    name = serializeColor( p.color(QPalette::ToolTipText) );
    m_theme["notes_fg"]  = Option(name, "VALUE", ui->pushButtonColorNotesFg);

    m_theme["font"]        = Option("", "VALUE", ui->pushButtonFont);
    m_theme["edit_font"]   = Option("", "VALUE", ui->pushButtonEditorFont);
    m_theme["find_font"]   = Option("", "VALUE", ui->pushButtonFoundFont);
    m_theme["num_font"]    = Option("", "VALUE", ui->pushButtonNumberFont);
    m_theme["notes_font"]  = Option("", "VALUE", ui->pushButtonNotesFont);
    m_theme["show_number"] = Option(true, "checked", ui->checkBoxShowNumber);
    m_theme["show_scrollbars"] = Option(true, "checked", ui->checkBoxScrollbars);

    m_theme["item_css"] = Option("");
    m_theme["alt_item_css"] = Option("");
    m_theme["sel_item_css"] = Option("");
    m_theme["notes_css"] = Option("");
    m_theme["css"] = Option("");

    m_theme["tab_tree_css"] = Option(
                "\n    ;color: ${fg}"
                "\n    ;background-color: ${bg}"
                "\n    ;selection-color: ${sel_fg}"
                "\n    ;selection-background-color: ${sel_bg}"
                );
    m_theme["tab_tree_item_css"] = Option("padding:2px");
    m_theme["tab_tree_sel_item_css"] = Option("");

    m_theme["use_system_icons"] = Option(false, "checked", ui->checkBoxSystemIcons);
}

QString ConfigTabAppearance::defaultUserThemePath() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QCoreApplication::organizationName(),
                       QCoreApplication::applicationName());
    return QDir::cleanPath(settings.fileName() + "/../themes");
}

QVariant ConfigTabAppearance::themeValue(const QString &name, const QHash<QString, Option> &theme) const
{
    return theme[name].value();
}

QColor ConfigTabAppearance::themeColor(const QString &name, const QHash<QString, Option> &theme) const
{
    return evalColor( themeValue(name, theme).toString(), theme );
}

QIcon ConfigTabAppearance::createThemeIcon(const QString &fileName)
{
    QHash<QString, Option> theme;
    foreach (const QString &key, m_theme.keys())
        theme[key].setValue( m_theme[key].value() );

    QSettings settings(fileName, QSettings::IniFormat);
    updateTheme(settings, &theme);

    QPixmap pix(16, 16);
    pix.fill(Qt::black);

    QPainter p(&pix);

    QRect rect(1, 1, 14, 5);
    p.setPen(Qt::NoPen);
    p.setBrush( themeColor("sel_bg", theme) );
    p.drawRect(rect);

    rect.translate(0, 5);
    p.setBrush( themeColor("bg", theme) );
    p.drawRect(rect);

    rect.translate(0, 5);
    p.setBrush( themeColor("alt_bg", theme) );
    p.drawRect(rect);

    QLine line;

    line = QLine(2, 3, 14, 3);
    QPen pen;
    p.setOpacity(0.6);

    pen.setColor( themeColor("sel_fg", theme) );
    pen.setDashPattern(QVector<qreal>() << 2 << 1 << 1 << 1 << 3 << 1 << 2 << 10);
    p.setPen(pen);
    p.drawLine(line);

    line.translate(0, 5);
    pen.setColor( themeColor("fg", theme) );
    pen.setDashPattern(QVector<qreal>() << 2 << 1 << 4 << 10);
    p.setPen(pen);
    p.drawLine(line);

    line.translate(0, 5);
    pen.setDashPattern(QVector<qreal>() << 3 << 1 << 2 << 1);
    p.setPen(pen);
    p.drawLine(line);

    return pix;
}
