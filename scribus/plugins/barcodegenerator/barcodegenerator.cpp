/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/


#include "../formatidlist.h"
#include "barcodegenerator.h"
#include "commonstrings.h"
#include "iconmanager.h"
#include "loadsaveplugin.h"
#include "scpaths.h"
#include "scribuscore.h"
#include "ui/paintmanager.h"
#include "undomanager.h"
#include "util_ghostscript.h"


#include <QTextStream>

BarcodeType::BarcodeType(const QString &cmd, const QString &exa,
	                     const QString &comm, const QString &regExp,
						 bool includeCheck, bool includeCheckInText)
{
	command = cmd;
	example = exa;
	comment = comm;
	regularExp = regExp;
	this->includeCheck = includeCheck;
	this->includeCheckInText = includeCheckInText;
}

BarcodeGenerator::BarcodeGenerator(QWidget* parent, const char* name)
	: QDialog(parent)
{
	ui.setupUi(this);
	setObjectName(name);
	setModal(true);
	map[tr("EAN-13")] = BarcodeType("ean13", "9780956078001", tr("12 or 13 digits"),
	                                "[0-9]{12,13}");
	map[tr("EAN-8")] = BarcodeType("ean8", "12345670", tr("8 digits"),
	                               "[0-9]{8,8}");
	map[tr("UPC-A")] = BarcodeType("upca", "78858101497", tr("11 or 12 digits"),
	                               "[0-9]{11,12}");
	map[tr("UPC-E")] = BarcodeType("upce", "0123456", tr("7 or 8 digits"),
	                               "[0-9]{7,8}");
	map[tr("EAN-5")] = BarcodeType("ean5", "90200", tr("5 digits"),
	                               "[0-9]{5,5}");
	map[tr("EAN-2")] = BarcodeType("ean2", "42", tr("2 digits"),
	                               "[0-9]{2,2}");
	map[tr("ISBN")] = BarcodeType("isbn", "978-0-9560780-0-1",
	                              tr("12 or 13 digits with dashes. The legacy ISBN-10 format accepts 9 or 10 digits with dashes, but this standard was depreciated for public use after 1st January 2007. (Note: To convert an old ISBN-10 to a new ISBN-13, prefix 978- to the first 9 digits, e.g. 1-56592-479-7 -> 978-1-56592-479. The final check-digit will be calculated automatically.)"),
	                              "[0-9]*\\-[0-9]*\\-[0-9]*");
//    "Code-11"] = "code11"
	map[tr("Code-39")] = BarcodeType("code39", "CODE-39",
	                                 tr("Variable number of characters, digits and any of the symbols -. *$/+%."),
	                                 "[0-9a-zA-Z\\-\\.\\ \\*\\$\\/\\+\\%]*",
	                                 true, true);
//    "Code-93"] = "code93"
	map[tr("Code-128")] = BarcodeType("code128", "^104^102Count^0991234^101!",
	                                  tr("Variable number of ASCII characters and special function symbols, starting with the appropriate start character for the initial character set. UCC/EAN-128s must have a mandatory FNC 1 symbol immediately following the start character."),
	                                  "\\^[0-9a-zA-Z\\^\\!]*",
	                                  true, true);
	map[tr("UCC/EAN-128")] = BarcodeType("code128", "^104^102Count^0991234^101!",
	                                     tr("Variable number of ASCII characters and special function symbols, starting with the appropriate start character for the initial character set. UCC/EAN-128s must have a mandatory FNC 1 symbol immediately following the start character."),
	                                     "\\^[0-9a-zA-Z\\^\\!]*");
	map[tr("Rationalized Codabar")] = BarcodeType("rationalizedCodabar", "A0123456789D",
	                                              tr("Variable number of digits and any of the symbols -$:/.+ABCD."),
	                                              "[0-9A-D\\-\\$\\:\\/\\.\\+]*",
	                                              true, true);
	map[tr("Interleaved 2 of 5")] = BarcodeType("interleaved2of5", "05012345678900",
	                                            tr("Variable number of digits"),
	                                            "[0-9]*",
	                                            true, true);
	map[tr("ITF-14")] = BarcodeType("interleaved2of5", "05012345678900",
	                                tr("Variable number of digits. An ITF-14 is 14 characters and does not have a check digit"),
	                                "[0-9]*",
	                                true, true);
	map[tr("Code 2 of 5")] = BarcodeType("code2of5", "0123456789",
	                                     tr("Variable number of digits"),
	                                     "[0-9]*");
	map[tr("Postnet")] = BarcodeType("postnet", "012345678",
	                                 tr("Variable number of digits"),
	                                 "[0-9]*",
	                                 false, true);
	map[tr("Royal Mail")] = BarcodeType("royalmail", "LE28HS9Z",
	                                    tr("Variable number of digits and capital letters"),
	                                    "[0-9A-Z]*",
	                                    false, true);
//    "Auspost"] = "auspost"
	map[tr("MSI")] = BarcodeType("msi", "0120823635162", tr("Variable number of digits"),
	                             "[0-9]*",
	                             true, true);
//    "KIX"] = "kix"
	map[tr("Plessey")] = BarcodeType("plessey", "012345ABCDEF",
	                                tr("Variable number of hexadecimal characters"),
	                                "[0-9A-F]*",
	                                false, true);
	map[tr("QR Code")] = BarcodeType("qrcode", "http://www.scribus.net",
	                                 tr("Variable number of characters, digits and any symbols."),
	                                 ".*",
	                                 false, false);
	//    "Symbol"] = "symbol"

	initializeQROptions();

	useSamples = true;
	guiColor = ui.codeEdit->palette().color(QPalette::Window);
	ui.bcCombo->addItem(tr("Barcode")); // to prevent 1st gs call
	ui.bcCombo->addItems(map.keys());
	ui.okButton->setText(CommonStrings::tr_OK);
	ui.cancelButton->setText(CommonStrings::tr_Cancel);
	ui.resetButton->setIcon(IconManager::instance()->loadIcon("u_undo16.png"));

	if (ScCore->primaryMainWindow()->doc->PageColors.contains("Black"))
	{
		lnColor = ScCore->primaryMainWindow()->doc->PageColors["Black"];
		txtColor = ScCore->primaryMainWindow()->doc->PageColors["Black"];
		ui.linesLabel->setToolTip("Black");
		ui.txtLabel->setToolTip("Black");
	}
	else
	{
		ui.linesLabel->setToolTip("n.a.");
		ui.txtLabel->setToolTip("n.a.");
	}
	if (ScCore->primaryMainWindow()->doc->PageColors.contains("White"))
	{
		bgColor = ScCore->primaryMainWindow()->doc->PageColors["White"];
		ui.bgLabel->setToolTip("White");
	}
	else
		ui.bgLabel->setToolTip("n.a.");

	paintColorSample(ui.linesLabel, lnColor);
	paintColorSample(ui.txtLabel, txtColor);
	paintColorSample(ui.bgLabel, bgColor);

	tmpFile = QDir::toNativeSeparators(ScPaths::getTempFileDir() + "bcode.png");
	psFile = QDir::toNativeSeparators(ScPaths::getTempFileDir() + "bcode.ps");

	// PS engine
	psCommand.append("%!PS-Adobe-2.0 EPSF-2.0\n");
	QFile f( ScPaths::instance().shareDir() + QString("/plugins/barcode.ps") );
	f.open(QIODevice::ReadOnly);
	QTextStream ts(&f);
	QString s = ts.readAll();
	int begin = s.indexOf("% --BEGIN TEMPLATE--");
	int end = s.indexOf("% --END TEMPLATE--");
	psCommand.append(s.mid(begin, end));
	f.close();
	psCommand.append("\n\n%command\n");
	bcComboChanged();
}

BarcodeGenerator::~BarcodeGenerator()
{
	QFile::remove(psFile);
	QFile::remove(tmpFile);
}

void BarcodeGenerator::initializeQROptions()
{
	QStringList qrEccKeys, qrEccValues;

	ui.formatCombo->addItem("Standard", "format=full");
	ui.formatCombo->addItem("Micro", "format=micro");

	ui.eccCombo->addItem("Auto", "");
	ui.eccCombo->addItem("L", "");
	ui.eccCombo->addItem("M", "eclevel=M");
	ui.eccCombo->addItem("Q", "eclevel=Q");
	ui.eccCombo->addItem("H", "eclevel=H");

	qrOptionsEnabled(false);
	connect(ui.formatCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(bcComboChanged(int)));
	connect(ui.eccCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(bcComboChanged(int)));
}

void BarcodeGenerator::qrOptionsEnabled(bool enabled) 
{
	ui.formatLabel->setEnabled(enabled);
	ui.formatCombo->setEnabled(enabled);
	ui.eccLabel->setEnabled(enabled);
	ui.eccCombo->setEnabled(enabled);
}

void BarcodeGenerator::bcComboChanged(int i)
{
	bcComboChanged();
}

void BarcodeGenerator::bcComboChanged()
{
	if (ui.bcCombo->currentIndex() == 0)
	{
		ui.okButton->setEnabled(false);
		ui.sampleLabel->setText(tr("Select Type"));
		return;
	}
	
	ui.okButton->setEnabled(true);

	QString s = ui.bcCombo->currentText();
	ui.commentEdit->setText(map[s].comment);
	if (useSamples)
	{
		disconnect(ui.codeEdit, SIGNAL(textChanged(const QString&)), this, SLOT(codeEdit_textChanged(const QString&)));
		ui.codeEdit->setText(map[s].example);
		connect(ui.codeEdit, SIGNAL(textChanged(const QString&)), this, SLOT(codeEdit_textChanged(const QString&)));
	}

	ui.includeCheck->setEnabled(map[s].includeCheck ? true : false);
	if (ui.textCheck->isChecked())
		ui.includeCheckInText->setEnabled(map[s].includeCheckInText ? true : false);
	else
		ui.includeCheckInText->setEnabled(false);

	if (QString::compare(s, "QR Code") == 0)
		qrOptionsEnabled(true);
	else
		qrOptionsEnabled(false);

	codeEdit_check(ui.codeEdit->text());
	paintBarcode();
}

void BarcodeGenerator::textCheck_changed()
{
	bool s = ui.textCheck->checkState();
	ui.txtColorButton->setEnabled(s);
	ui.includeCheckInText->setEnabled(s);
	paintBarcode();
}

void BarcodeGenerator::guardCheck_changed()
{
	paintBarcode();
}

void BarcodeGenerator::includeCheck_stateChanged(int)
{
	paintBarcode();
}

void BarcodeGenerator::includeCheckInText_stateChanged(int)
{
	paintBarcode();
}

void BarcodeGenerator::paintColorSample(QLabel *l, const ScColor & c)
{
	QRect rect = l->frameRect();
	QPixmap pm(rect.width(), rect.height());
	pm.fill(c.getRawRGBColor()); // brute force sc2qt color convert for preview
	l->setPixmap(pm);
}

void BarcodeGenerator::bgColorButton_pressed()
{
	PaintManagerDialog d(this, &ScCore->primaryMainWindow()->doc->docGradients, ScCore->primaryMainWindow()->doc->PageColors, "", &ScCore->primaryMainWindow()->doc->docPatterns, ScCore->primaryMainWindow()->doc, ScCore->primaryMainWindow());
	if (!d.exec())
		return;
	bgColor = d.selectedColor();
	ui.bgLabel->setToolTip(d.selectedColorName());
	paintColorSample(ui.bgLabel, bgColor);
	paintBarcode();
}

void BarcodeGenerator::lnColorButton_pressed()
{
	PaintManagerDialog d(this, &ScCore->primaryMainWindow()->doc->docGradients, ScCore->primaryMainWindow()->doc->PageColors, "", &ScCore->primaryMainWindow()->doc->docPatterns, ScCore->primaryMainWindow()->doc, ScCore->primaryMainWindow());
	if (!d.exec())
		return;
	lnColor = d.selectedColor();
	ui.linesLabel->setToolTip(d.selectedColorName());
	paintColorSample(ui.linesLabel, lnColor);
	paintBarcode();
}

void BarcodeGenerator::txtColorButton_pressed()
{
	PaintManagerDialog d(this, &ScCore->primaryMainWindow()->doc->docGradients, ScCore->primaryMainWindow()->doc->PageColors, "", &ScCore->primaryMainWindow()->doc->docPatterns, ScCore->primaryMainWindow()->doc, ScCore->primaryMainWindow());

	if (!d.exec())
		return;

	txtColor = d.selectedColor();
	ui.txtLabel->setToolTip(d.selectedColorName());
	paintColorSample(ui.txtLabel, txtColor);
	paintBarcode();
}

void BarcodeGenerator::okButton_pressed()
{
	// no need to call paintBarcode(tmpFile, 300); because
	// it's created by previous run...
	hide();
	const FileFormat * fmt = LoadSavePlugin::getFormatByExt("ps");

	UndoTransaction tran;
	if (UndoManager::undoEnabled())
	{
		tran = UndoManager::instance()->beginTransaction(
							ScCore->primaryMainWindow()->doc->currentPage()->getUName(),
							Um::IImageFrame,
							Um::ImportBarcode,
							ui.bcCombo->currentText() + " (" + ui.codeEdit->text() + ")",
							Um::IEPS);
	}

	if (fmt)
	{
		fmt->loadFile(psFile, LoadSavePlugin::lfUseCurrentPage|LoadSavePlugin::lfInteractive);
		if (tran)
			tran.commit();
	}
	accept();
}

void BarcodeGenerator::cancelButton_pressed()
{
	reject();
}

bool BarcodeGenerator::codeEdit_check(const QString& )//s)
{
	/* probably not needed as the backend do it for us (PV)
	QRegExp rx(map[bcCombo->currentText()].regularExp);
	if (!rx.exactMatch(s))
	{
		ui.codeEdit->setPaletteBackgroundColor(QColor(255, 0, 0));
		ui.sampleLabel->setText("<qt>" + tr("Barcode incomplete") + "</qt>");
		ui.okButton->setEnabled(false);
		return false;
	}
	else
	{
		ui.codeEdit->setPaletteBackgroundColor(guiColor);
		ui.okButton->setEnabled(true);
		paintBarcode();
		return true;
	} */
	paintBarcode();
	return true;
}

void BarcodeGenerator::codeEdit_textChanged(const QString& s)
{
	useSamples = false;
	codeEdit_check(s);
}

bool BarcodeGenerator::paintBarcode(const QString &fileName, int dpi)
{
	QString realFile(fileName);
	if (fileName.isEmpty())
	    realFile = tmpFile;
	QString opts("barcolor=%1 showbackground backgroundcolor=%2 textcolor=%3");
	opts = opts.arg(lnColor.name().replace('#', "")) \
			.arg(bgColor.name().replace('#', "")) \
			.arg(txtColor.name().replace('#', ""));
	if (ui.textCheck->isChecked())
		opts += " includetext";
	if (ui.guardCheck->isChecked())
		opts += " guardwhitespace";
	if (ui.includeCheckInText->isChecked() & ui.includeCheckInText->isEnabled())
		opts += " includecheckintext";
	if (ui.includeCheck->isChecked() & ui.includeCheck->isEnabled())
		opts += " includecheck";
	if (ui.formatCombo->isEnabled())
	{
		int index = ui.formatCombo->currentIndex();
		QVariant currentData = ui.formatCombo->itemData(index);
		opts += " " + currentData.toString();
	}
	if (ui.eccCombo->isEnabled())
	{
		int index = ui.eccCombo->currentIndex();
		QVariant currentData = ui.eccCombo->itemData(index);
		opts += " " + currentData.toString();
	}
	QString comm("15 10 moveto (%1) (%2) /%3 /uk.co.terryburton.bwipp findresource exec");
	comm = comm.arg(ui.codeEdit->text()).arg(opts).arg(map[ui.bcCombo->currentText()].command);
	comm = psCommand + comm;
	QFile f(psFile);
	if (!f.open(QIODevice::WriteOnly))
	{
		ui.sampleLabel->setText("<qt>" + tr("Error opening file: %1").arg(psFile) + "</qt>");
		return false;
	}
	QTextStream ts(&f);
	ts << comm;
	f.close();

	QStringList gargs;
	// limit the area only for preview. EPS importer bounds the box itself.
	if (realFile == tmpFile)
	{
		gargs.append("-dDEVICEWIDTHPOINTS=200");
		gargs.append("-dDEVICEHEIGHTPOINTS=150");
	}
	gargs.append( QString("-r%1").arg(dpi) );
	gargs.append( QString("-sOutputFile=%1").arg(realFile) );
	gargs.append( psFile );
	qApp->changeOverrideCursor(Qt::WaitCursor);
	int gs = callGS(gargs);
	bool retval = true;
	if (gs != 0)
		retval = false;
	qApp->changeOverrideCursor(Qt::ArrowCursor);
//	qApp->restoreOverrideCursor();
	// setup only preview
	if (realFile != tmpFile)
		return retval;
	if (gs == 0)
	{
	    ui.sampleLabel->setPixmap(QPixmap(realFile));
		ui.okButton->setEnabled(true);
	}
	else
	{
		ui.sampleLabel->setText("<qt>" + tr("Barcode incomplete") + "</qt>");
		ui.okButton->setEnabled(false);
	}
	return retval;
}

void BarcodeGenerator::resetButton_clicked()
{
	useSamples = true;
	bcComboChanged();
}
