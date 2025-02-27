################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2024, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

from qtpy import QtCore, QtGui, QtWidgets

from armoryengine.ArmoryUtils import toUnicode, USE_TESTNET, \
   USE_REGTEST, LOGEXCEPT
from armoryengine.AddressUtils import binary_to_base58, encodePrivKeyBase58, \
   hash160_to_addrStr
from armorycolors import htmlColor, Colors

from ui.WalletFrames import WalletBackupFrame
from ui.QrCodeMatrix import CreateQRMatrix

from qtdialogs.qtdefines import makeHorizFrame, QRichLabel, \
   makeVertFrame, QImageLabel, HLINE, GETFONT, STYLE_RAISED, tightSizeStr, \
   setLayoutStretch, STRETCH, createToolTipWidget
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgUnlockWallet import UnlockWalletHandler
from qtdialogs.DlgRestore import OpenPaperBackupDialog


################################################################################
class DlgBackupCenter(ArmoryDialog):

   #############################################################################
   def __init__(self, parent, main, wlt):
      super(DlgBackupCenter, self).__init__(parent, main)

      self.wlt = wlt
      wltID = wlt.walletId
      wltName = wlt.labelName

      self.walletBackupFrame = WalletBackupFrame(parent, main)
      self.walletBackupFrame.setWallet(wlt)
      self.btnDone = QtWidgets.QPushButton(self.tr('Done'))
      self.btnDone.clicked.connect(self.reject)
      frmBottomBtns = makeHorizFrame([STRETCH, self.btnDone])

      layoutDialog = QtWidgets.QVBoxLayout()

      layoutDialog.addWidget(self.walletBackupFrame)

      layoutDialog.addWidget(frmBottomBtns)

      self.setLayout(layoutDialog)
      self.setWindowTitle(self.tr("Backup Center"))
      self.setMinimumSize(640, 350)

################################################################################
class DlgSimpleBackup(ArmoryDialog):

   #############################################################################
   def __init__(self, parent, main, wlt):
      super(DlgSimpleBackup, self).__init__(parent, main)

      self.wlt = wlt

      lblDescrTitle = QRichLabel(self.tr(
         '<b>Protect Your Bitcoins -- Make a Wallet Backup!</b>'))

      lblDescr = QRichLabel(self.tr(
         'A failed hard-drive or forgotten passphrase will lead to '
         '<u>permanent loss of bitcoins</u>!  Luckily, Armory wallets only '
         'need to be backed up <u>one time</u>, and protect you in both '
         'of these events.   If you\'ve ever forgotten a password or had '
         'a hardware failure, make a backup!'))

      # ## Paper
      lblPaper = QRichLabel(self.tr(
         'Use a printer or pen-and-paper to write down your wallet "seed."'))
      btnPaper = QtWidgets.QPushButton(self.tr('Make Paper Backup'))

      # ## Digital
      lblDigital = QRichLabel(self.tr(
         'Create an unencrypted copy of your wallet file, including imported '
         'addresses.'))
      btnDigital = QtWidgets.QPushButton(self.tr('Make Digital Backup'))

      # ## Other
      btnOther = QtWidgets.QPushButton(self.tr('See Other Backup Options'))

      def backupDigital():
         if self.main.digitalBackupWarning():
            self.main.makeWalletCopy(self, self.wlt, 'Decrypt', 'decrypt')
            self.accept()

      def backupPaper():
         OpenPaperBackupDialog('Single', self, self.main, self.wlt)
         self.accept()

      def backupOther():
         self.accept()
         DlgBackupCenter(self, self.main, self.wlt).exec_()

      btnPaper.connect.clicked(backupPaper)
      btnDigital.connect.clicked(backupDigital)
      btnOther.connect.clicked(backupOther)

      layout = QtWidgets.QGridLayout()

      layout.addWidget(lblPaper, 0, 0)
      layout.addWidget(btnPaper, 0, 2)

      layout.addWidget(HLINE(), 1, 0, 1, 3)

      layout.addWidget(lblDigital, 2, 0)
      layout.addWidget(btnDigital, 2, 2)

      layout.addWidget(HLINE(), 3, 0, 1, 3)

      layout.addWidget(makeHorizFrame([STRETCH, btnOther, STRETCH]), 4, 0, 1, 3)

      # layout.addWidget( VLINE(),      0,1, 5,1)

      layout.setContentsMargins(10, 5, 10, 5)
      setLayoutStretchRows(layout, 1, 0, 1, 0, 0)
      setLayoutStretchCols(layout, 1, 0, 0)

      frmGrid = QtWidgets.QFrame()
      frmGrid.setFrameStyle(STYLE_PLAIN)
      frmGrid.setLayout(layout)

      btnClose = QtWidgets.QPushButton(self.tr('Done'))
      btnClose.connect.clicked(self.accept)
      frmClose = makeHorizFrame([STRETCH, btnClose])

      frmAll = makeVertFrame([lblDescrTitle, lblDescr, frmGrid, frmClose])
      layoutAll = QtWidgets.QVBoxLayout()
      layoutAll.addWidget(frmAll)
      self.setLayout(layoutAll)
      self.sizeHint = lambda: QtCore.QSize(400, 250)

      self.setWindowTitle(self.tr('Backup Options'))

################################################################################
class SimplePrintableGraphicsScene(object):

   #############################################################################
   def __init__(self, parent, main):
      """
      We use the following coordinates:

            -----> +x
            |
            |
            V +y

      """
      self.parent = parent
      self.main = main

      self.INCH = 72
      self.PAPER_A4_WIDTH = 8.5 * self.INCH
      self.PAPER_A4_HEIGHT = 11.0 * self.INCH
      self.MARGIN_PIXELS = 0.6 * self.INCH

      self.PAGE_BKGD_COLOR = Colors.White
      self.PAGE_TEXT_COLOR = Colors.Black

      self.fontFix = GETFONT('Courier', 9)
      self.fontVar = GETFONT('Times', 10)

      self.gfxScene = QtWidgets.QGraphicsScene(self.parent)
      self.gfxScene.setSceneRect(0, 0, self.PAPER_A4_WIDTH, self.PAPER_A4_HEIGHT)
      self.gfxScene.setBackgroundBrush(self.PAGE_BKGD_COLOR)

      # For when it eventually makes it to the printer
      # self.printer = QPrinter(QPrinter.HighResolution)
      # self.printer.setPageSize(QPrinter.Letter)
      # self.gfxPainter = QtGui.QPainter(self.printer)
      # self.gfxPainter.setRenderHint(QtGui.QPainter.TextAntialiasing)
      # self.gfxPainter.setPen(QtCore.Qt.NoPen)
      # self.gfxPainter.setBrush(QtGui.QBrush(self.PAGE_TEXT_COLOR))

      self.cursorPos = QtCore.QPointF(self.MARGIN_PIXELS, self.MARGIN_PIXELS)
      self.lastCursorMove = (0, 0)

   def getCursorXY(self):
      return (self.cursorPos.x(), self.cursorPos.y())

   def getScene(self):
      return self.gfxScene

   def pageRect(self):
      marg = self.MARGIN_PIXELS
      return QtCore.QRectF(marg, marg, self.PAPER_A4_WIDTH - marg, self.PAPER_A4_HEIGHT - marg)

   def insidePageRect(self, pt=None):
      if pt == None:
         pt = self.cursorPos

      return self.pageRect.contains(pt)

   def moveCursor(self, dx, dy, absolute=False):
      xOld, yOld = self.getCursorXY()
      if absolute:
         self.cursorPos = QtCore.QPointF(dx, dy)
         self.lastCursorMove = (dx - xOld, dy - yOld)
      else:
         self.cursorPos = QtCore.QPointF(xOld + dx, yOld + dy)
         self.lastCursorMove = (dx, dy)

   def resetScene(self):
      self.gfxScene.clear()
      self.resetCursor()

   def resetCursor(self):
      self.cursorPos = QtCore.QPointF(self.MARGIN_PIXELS, self.MARGIN_PIXELS)

   def newLine(self, extra_dy=0):
      xOld, yOld = self.getCursorXY()
      xNew = self.MARGIN_PIXELS
      yNew = self.cursorPos.y() + self.lastItemSize[1] + extra_dy - 5
      self.moveCursor(xNew - xOld, yNew - yOld)


   def drawHLine(self, width=None, penWidth=1):
      if width == None:
         width = 3 * self.INCH
      currX, currY = self.getCursorXY()
      lineItem = QtWidgets.QGraphicsLineItem(currX, currY, currX + width, currY)
      pen = QtGui.QPen()
      pen.setWidth(penWidth)
      lineItem.setPen(pen)
      self.gfxScene.addItem(lineItem)
      rect = lineItem.boundingRect()
      self.lastItemSize = (rect.width(), rect.height())
      self.moveCursor(rect.width(), 0)
      return self.lastItemSize

   def drawRect(self, w, h, edgeColor=Colors.Black, fillColor=None, penWidth=1):
      rectItem = QtWidgets.QGraphicsRectItem(self.cursorPos.x(), self.cursorPos.y(), w, h)
      if edgeColor == None:
         rectItem.setPen(QtGui.QPen(QtCore.Qt.NoPen))
      else:
         pen = QtGui.QPen(edgeColor)
         pen.setWidth(penWidth)
         rectItem.setPen(pen)

      if fillColor == None:
         rectItem.setBrush(QtGui.QBrush(QtCore.Qt.NoBrush))
      else:
         rectItem.setBrush(QtGui.QBrush(fillColor))

      self.gfxScene.addItem(rectItem)
      rect = rectItem.boundingRect()
      self.lastItemSize = (rect.width(), rect.height())
      self.moveCursor(rect.width(), 0)
      return self.lastItemSize


   def drawText(self, txt, font=None, wrapWidth=None, useHtml=True):
      if font == None:
         font = GETFONT('Var', 9)
      txtItem = QtWidgets.QGraphicsTextItem('')
      if useHtml:
         txtItem.setHtml(toUnicode(txt))
      else:
         txtItem.setPlainText(toUnicode(txt))
      txtItem.setDefaultTextColor(self.PAGE_TEXT_COLOR)
      txtItem.setPos(self.cursorPos)
      txtItem.setFont(font)
      if not wrapWidth == None:
         txtItem.setTextWidth(wrapWidth)
      self.gfxScene.addItem(txtItem)
      rect = txtItem.boundingRect()
      self.lastItemSize = (rect.width(), rect.height())
      self.moveCursor(rect.width(), 0)
      return self.lastItemSize

   def drawPixmapFile(self, pixFn, sizePx=None):
      pix = QtGui.QPixmap(pixFn)
      if not sizePx == None:
         pix = pix.scaled(sizePx, sizePx)
      pixItem = QtWidgets.QGraphicsPixmapItem(pix)
      pixItem.setPos(self.cursorPos)
      self.gfxScene.addItem(pixItem)
      rect = pixItem.boundingRect()
      self.lastItemSize = (rect.width(), rect.height())
      self.moveCursor(rect.width(), 0)
      return self.lastItemSize

   def drawQR(self, qrdata, size=150):
      objQR = GfxItemQRCode(qrdata, size)
      objQR.setPos(self.cursorPos)
      self.gfxScene.addItem(objQR)
      rect = objQR.boundingRect()
      self.lastItemSize = (rect.width(), rect.height())
      self.moveCursor(rect.width(), 0)
      return self.lastItemSize


   def drawColumn(self, strList, rowHeight=None, font=None, useHtml=True):
      """
      This draws a bunch of left-justified strings in a column.  It returns
      a tight bounding box around all elements in the column, which can easily
      be used to start the next column.  The rowHeight is returned, and also
      an available input, in case you are drawing text/font that has a different
      height in each column, and want to make sure they stay aligned.

      Just like the other methods, this leaves the cursor sitting at the
      original y-value, but shifted to the right by the width of the column.
      """
      origX, origY = self.getCursorXY()
      maxColWidth = 0
      cumulativeY = 0
      for r in strList:
         szX, szY = self.drawText(r, font=font, useHtml=useHtml)
         prevY = self.cursorPos.y()
         if rowHeight == None:
            self.newLine()
            szY = self.cursorPos.y() - prevY
            self.moveCursor(origX - self.MARGIN_PIXELS, 0)
         else:
            self.moveCursor(-szX, rowHeight)
         maxColWidth = max(maxColWidth, szX)
         cumulativeY += szY

      if rowHeight == None:
         rowHeight = float(cumulativeY) / len(strList)

      self.moveCursor(origX + maxColWidth, origY, absolute=True)

      return [QtCore.QRectF(origX, origY, maxColWidth, cumulativeY), rowHeight]

################################################################################
class GfxItemQRCode(QtWidgets.QGraphicsItem):
   """
   Converts binary data to base58, and encodes the Base58 characters in
   the QR-code.  It seems weird to use Base58 instead of binary, but the
   QR-code has no problem with the size, instead, we want the data in the
   QR-code to match exactly what is human-readable on the page, which is
   in Base58.

   You must supply exactly one of "totalSize" or "modSize".  TotalSize
   guarantees that the QR code will fit insides box of a given size.
   ModSize is how big each module/pixel of the QR code is, which means
   that a bigger QR block results in a bigger physical size on paper.
   """
   def __init__(self, rawDataToEncode, maxSize=None):
      super(GfxItemQRCode, self).__init__()
      self.maxSize = maxSize
      self.updateQRData(rawDataToEncode)

   def boundingRect(self):
      return self.Rect

   def updateQRData(self, toEncode, maxSize=None):
      if maxSize == None:
         maxSize = self.maxSize
      else:
         self.maxSize = maxSize

      self.qrmtrx, self.modCt = CreateQRMatrix(toEncode, 'H')
      self.modSz = round(float(self.maxSize) / float(self.modCt) - 0.5)
      totalSize = self.modCt * self.modSz
      self.Rect = QtCore.QRectF(0, 0, totalSize, totalSize)

   def paint(self, painter, option, widget=None):
      painter.setPen(QtCore.Qt.NoPen)
      painter.setBrush(QtGui.QBrush(Colors.Black))

      for r in range(self.modCt):
         for c in range(self.modCt):
            if self.qrmtrx[r][c] > 0:
               painter.drawRect(*[self.modSz * a for a in [r, c, 1, 1]])

################################################################################
class DlgPrintBackup(ArmoryDialog):
   """
   Open up a "Make Paper Backup" dialog, so the user can print out a hard
   copy of whatever data they need to recover their wallet should they lose
   it.

   This method is kind of a mess, because it ended up having to support
   printing of single-sheet, imported keys, single fragments, multiple
   fragments, with-or-without SecurePrint.
   """
   def __init__(self, parent, main, wlt, printType='SingleSheet', \
                                    fragMtrx=[], fragMtrxCrypt=[], fragData=[],
                                    privKey=None, chaincode=None):
      super(DlgPrintBackup, self).__init__(parent, main)


      self.wlt = wlt
      self.binImport = []
      self.fragMtrx = fragMtrx

      self.doPrintFrag = printType.lower().startswith('frag')
      self.fragMtrx = fragMtrx
      self.fragMtrxCrypt = fragMtrxCrypt
      self.fragData = fragData
      if self.doPrintFrag:
         self.doMultiFrag = len(fragData['Range']) > 1

      self.backupData = None
      def resumeSetup(reply):
         self.backupData = None
         if reply.success:
            self.backupData = reply.wallet.createBackupString
         self.executeMethod(self.setup)

      unlockHandler = UnlockWalletHandler(self.wlt.walletId,
         "Create Backup", self)
      self.wlt.createBackupString(unlockHandler, resumeSetup)

   ###
   def setup(self):
      # This badBackup stuff was implemented to avoid making backups if there is
      # an inconsistency in the data.  Yes, this is like a goto!
      if self.backupData == None:
         LOGEXCEPT("Problem with private key and/or chaincode.  Aborting.")
         QtWidgets.QMessageBox.critical(self, self.tr("Error Creating Backup"), self.tr(
            'There was an error with the backup creator.  The operation is being '
            'canceled to avoid making bad backups!'), QtWidgets.QMessageBox.Ok)
         self.reject()
         return

      # A self-evident check of whether we need to print the chaincode.
      # If we derive the chaincode from the private key, and it matches
      # what's already in the wallet, we obviously don't need to print it!
      self.noNeedChaincode = (len(self.backupData.chainClear) == 0)

      # Save off imported addresses in case they need to be printed, too
      for a160, addr in self.wlt.addrMap.items():
         if addr.chainIndex == -2:
            if addr.binPrivKey32_Plain.getSize() == 33 or addr.isCompressed():
               prv = addr.binPrivKey32_Plain.toBinStr()[:32]
               self.binImport.append([a160, SecureBinaryData(prv), 1])
               prv = None
            else:
               self.binImport.append([a160, addr.binPrivKey32_Plain.copy(), 0])


      tempTxtItem = QtWidgets.QGraphicsTextItem('')
      tempTxtItem.setPlainText(toUnicode('0123QAZjqlmYy'))
      tempTxtItem.setFont(GETFONT('Fix', 7))
      self.importHgt = tempTxtItem.boundingRect().height() - 5


      # Create the scene and the view.
      self.scene = SimplePrintableGraphicsScene(self, self.main)
      self.view = QtWidgets.QGraphicsView()
      self.view.setRenderHint(QtGui.QPainter.TextAntialiasing)
      self.view.setScene(self.scene.getScene())


      self.chkImportPrint = QtWidgets.QCheckBox(self.tr('Print imported keys'))
      self.chkImportPrint.clicked.connect(self.clickImportChk)

      self.lblPageStr = QRichLabel(self.tr('Page:'))
      self.comboPageNum = QtWidgets.QComboBox()
      self.lblPageMaxStr = QRichLabel('')
      self.comboPageNum.activated.connect(self.redrawBackup)

      # We enable printing of imported addresses but not frag'ing them.... way
      # too much work for everyone (developer and user) to deal with 2x or 3x
      # the amount of data to type
      self.chkImportPrint.setVisible(len(self.binImport) > 0 and not self.doPrintFrag)
      self.lblPageStr.setVisible(False)
      self.comboPageNum.setVisible(False)
      self.lblPageMaxStr.setVisible(False)

      self.chkSecurePrint = QtWidgets.QCheckBox(self.tr(u'Use SecurePrint\u200b\u2122 to prevent exposing keys to printer or other '
         'network devices'))

      if(self.doPrintFrag):
         self.chkSecurePrint.setChecked(self.fragData['Secure'])

      self.ttipSecurePrint = createToolTipWidget(self.tr(
         u'SecurePrint\u200b\u2122 encrypts your backup with a code displayed on '
         'the screen, so that no other devices on your network see the sensitive '
         'data when you send it to the printer.  If you turn on '
         u'SecurePrint\u200b\u2122 <u>you must write the code on the page after '
         'it is done printing!</u>  There is no point in using this feature if '
         'you copy the data by hand.'))

      self.lblSecurePrint = QRichLabel(self.tr(
         u'<b><font color="%s"><u>IMPORTANT:</u></b>  You must write the SecurePrint\u200b\u2122 '
         u'encryption code on each printed backup page!  Your SecurePrint\u200b\u2122 code is </font> '
         '<font color="%s">%s</font>.  <font color="%s">Your backup will not work '
         'if this code is lost!</font>' % (htmlColor('TextWarn'), htmlColor('TextBlue'), \
         self.backupData.spPass, htmlColor('TextWarn'))))

      self.chkSecurePrint.clicked.connect(self.redrawBackup)


      self.btnPrint = QtWidgets.QPushButton('&Print...')
      self.btnPrint.setMinimumWidth(3 * tightSizeStr(self.btnPrint, 'Print...')[0])
      self.btnCancel = QtWidgets.QPushButton('&Cancel')
      self.btnPrint.clicked.connect(self.print_)
      self.btnCancel.clicked.connect(self.accept)

      if self.doPrintFrag:
         M, N = self.fragData['M'], self.fragData['N']
         lblDescr = QRichLabel(self.tr(
            '<b><u>Print Wallet Backup Fragments</u></b><br><br> '
            'When any %s of these fragments are combined, all <u>previous '
            '<b>and</b> future</u> addresses generated by this wallet will be '
            'restored, giving you complete access to your bitcoins.  The '
            'data can be copied by hand if a working printer is not '
            'available.  Please make sure that all data lines contain '
            '<b>9 columns</b> '
            'of <b>4 characters each</b> (excluding "ID" lines).' % M))
      else:
         withChain = '' if self.noNeedChaincode else 'and "Chaincode"'
         lblDescr = QRichLabel(self.tr(
            '<b><u>Print a Forever-Backup</u></b><br><br> '
            'Printing this sheet protects all <u>previous <b>and</b> future</u> addresses '
            'generated by this wallet!  You can copy the "Root Key" %s '
            'by hand if a working printer is not available.  Please make sure that '
            'all data lines contain <b>9 columns</b> '
            'of <b>4 characters each</b>.' % withChain))

      lblDescr.setContentsMargins(5, 5, 5, 5)
      frmDescr = makeHorizFrame([lblDescr], STYLE_RAISED)

      self.redrawBackup()
      frmChkImport = makeHorizFrame([self.chkImportPrint, \
                                     STRETCH, \
                                     self.lblPageStr, \
                                     self.comboPageNum, \
                                     self.lblPageMaxStr])

      frmSecurePrint = makeHorizFrame([self.chkSecurePrint,
                                       self.ttipSecurePrint,
                                       STRETCH])

      frmButtons = makeHorizFrame([self.btnCancel, STRETCH, self.btnPrint])

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(frmDescr)
      layout.addWidget(frmChkImport)
      layout.addWidget(self.view)
      layout.addWidget(frmSecurePrint)
      layout.addWidget(self.lblSecurePrint)
      layout.addWidget(frmButtons)
      setLayoutStretch(layout, 0, 1, 0, 0, 0)

      self.setLayout(layout)

      self.setWindowIcon(QtGui.QIcon('./img/printer_icon.png'))
      self.setWindowTitle('Print Wallet Backup')

      # Apparently I can't programmatically scroll until after it's painted
      def scrollTop():
         vbar = self.view.verticalScrollBar()
         vbar.setValue(vbar.minimum())
      self.callLater(0.01, scrollTop)

   def redrawBackup(self):
      cmbPage = 1
      if self.comboPageNum.count() > 0:
         cmbPage = int(str(self.comboPageNum.currentText()))

      if self.doPrintFrag:
         cmbPage -= 1
         if not self.doMultiFrag:
            cmbPage = self.fragData['Range'][0]
         elif self.comboPageNum.count() > 0:
            cmbPage = int(str(self.comboPageNum.currentText())) - 1

         self.createPrintScene('Fragmented Backup', cmbPage)
      else:
         pgSelect = cmbPage if self.chkImportPrint.isChecked() else 1
         if pgSelect == 1:
            self.createPrintScene('SingleSheetFirstPage', '')
         else:
            pg = pgSelect - 2
            nKey = self.maxKeysPerPage
            self.createPrintScene('SingleSheetImported', [pg * nKey, (pg + 1) * nKey])


      showPageCombo = self.chkImportPrint.isChecked() or \
                      (self.doPrintFrag and self.doMultiFrag)
      self.showPageSelect(showPageCombo)
      self.view.update()

   def clickImportChk(self):
      if self.numImportPages > 1 and self.chkImportPrint.isChecked():
         ans = QtWidgets.QMessageBox.warning(self, self.tr('Lots to Print!'), self.tr(
            'This wallet contains <b>%d</b> imported keys, which will require '
            '<b>%d</b> pages to print.  Not only will this use a lot of paper, '
            'it will be a lot of work to manually type in these keys in the '
            'event that you need to restore this backup. It is recommended '
            'that you do <u>not</u> print your imported keys and instead make '
            'a digital backup, which can be restored instantly if needed. '
            '<br><br> Do you want to print the imported keys, anyway?' % (len(self.binImport), self.numImportPages)), \
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
         if not ans == QtWidgets.QMessageBox.Yes:
            self.chkImportPrint.setChecked(False)

      showPageCombo = self.chkImportPrint.isChecked() or \
                      (self.doPrintFrag and self.doMultiFrag)
      self.showPageSelect(showPageCombo)
      self.comboPageNum.setCurrentIndex(0)
      self.redrawBackup()

   def showPageSelect(self, doShow=True):
      MARGIN = self.scene.MARGIN_PIXELS
      bottomOfPage = self.scene.pageRect().height() + MARGIN
      totalHgt = bottomOfPage - self.bottomOfSceneHeader
      self.maxKeysPerPage = int(totalHgt / (self.importHgt))
      self.numImportPages = int((len(self.binImport) - 1) / self.maxKeysPerPage) + 1
      if self.comboPageNum.count() == 0:
         if self.doPrintFrag:
            numFrag = len(self.fragData['Range'])
            for i in range(numFrag):
               self.comboPageNum.addItem(str(i + 1))
            self.lblPageMaxStr.setText(self.tr('of %d' % numFrag))
         else:
            for i in range(self.numImportPages + 1):
               self.comboPageNum.addItem(str(i + 1))
            self.lblPageMaxStr.setText(self.tr('of %d' % (self.numImportPages + 1)))


      self.lblPageStr.setVisible(doShow)
      self.comboPageNum.setVisible(doShow)
      self.lblPageMaxStr.setVisible(doShow)

   def print_(self):
      LOGINFO('Printing!')
      self.printer = QPrinter(QPrinter.HighResolution)
      self.printer.setPageSize(QPrinter.Letter)

      if QPrintDialog(self.printer).exec_():
         painter = QtGui.QPainter(self.printer)
         painter.setRenderHint(QtGui.QPainter.TextAntialiasing)

         if self.doPrintFrag:
            for i in self.fragData['Range']:
               self.createPrintScene('Fragment', i)
               self.scene.getScene().render(painter)
               if not i == len(self.fragData['Range']) - 1:
                  self.printer.newPage()

         else:
            self.createPrintScene('SingleSheetFirstPage', '')
            self.scene.getScene().render(painter)

            if len(self.binImport) > 0 and self.chkImportPrint.isChecked():
               nKey = self.maxKeysPerPage
               for i in range(self.numImportPages):
                  self.printer.newPage()
                  self.createPrintScene('SingleSheetImported', [i * nKey, (i + 1) * nKey])
                  self.scene.getScene().render(painter)

         painter.end()

         # The last scene printed is what's displayed now.  Set the combo box
         self.comboPageNum.setCurrentIndex(self.comboPageNum.count() - 1)

         if self.chkSecurePrint.isChecked():
            QtWidgets.QMessageBox.warning(self, self.tr('SecurePrint Code'), self.tr(
               u'<br><b>You must write your SecurePrint\u200b\u2122 '
               'code on each sheet of paper you just printed!</b> '
               'Write it in the red box in upper-right corner '
               u'of each printed page. <br><br>SecurePrint\u200b\u2122 code: '
               '<font color="%s" size=5><b>%s</b></font> <br><br> '
               '<b>NOTE: the above code <u>is</u> case-sensitive!</b>' % (htmlColor('TextBlue'), self.randpass.toBinStr())), \
               QtWidgets.QMessageBox.Ok)
         if self.chkSecurePrint.isChecked():
            self.btnCancel.setText('Done')
         else:
            self.accept()

   def cleanup(self):
      self.backupData = None

      for x, y in self.fragMtrxCrypt:
         x.destroy()
         y.destroy()

   def accept(self):
      self.cleanup()
      super(DlgPrintBackup, self).accept()

   def reject(self):
      self.cleanup()
      super(DlgPrintBackup, self).reject()


   #############################################################################
   #############################################################################
   def createPrintScene(self, printType, printData):
      self.scene.gfxScene.clear()
      self.scene.resetCursor()

      pr = self.scene.pageRect()
      self.scene.drawRect(pr.width(), pr.height(), edgeColor=None, fillColor=Colors.White)
      self.scene.resetCursor()


      INCH = self.scene.INCH
      MARGIN = self.scene.MARGIN_PIXELS

      doMask = self.chkSecurePrint.isChecked()

      if USE_TESTNET or USE_REGTEST:
         self.scene.drawPixmapFile('./img/armory_logo_green_h56.png')
      else:
         self.scene.drawPixmapFile('./img/armory_logo_h36.png')
      self.scene.newLine()

      self.scene.drawText('Paper Backup for Armory Wallet', GETFONT('Var', 11))
      self.scene.newLine()

      self.scene.newLine(extra_dy=20)
      self.scene.drawHLine()
      self.scene.newLine(extra_dy=20)


      ssType = self.tr(u' (SecurePrint\u200b\u2122)') if doMask else self.tr(' (Unencrypted)')
      if printType == 'SingleSheetFirstPage':
         bType = self.tr('Single-Sheet') # %s' % ssType)
      elif printType == 'SingleSheetImported':
         bType = self.tr('Imported Keys %s' % ssType)
      elif printType.lower().startswith('frag'):
         m_count = str(self.fragData['M'])
         n_count = str(self.fragData['N'])
         bstr = self.tr('Fragmented Backup (%s-of-%s)' % (m_count, n_count))
         bType = bstr + ' ' + ssType

      if printType.startswith('SingleSheet'):
         colRect, rowHgt = self.scene.drawColumn(['Wallet Version:', 'Wallet ID:', \
                                                   'Wallet Name:', 'Backup Type:'])
         self.scene.moveCursor(15, 0)
         suf = 'c' if self.noNeedChaincode else 'a'
         colRect, rowHgt = self.scene.drawColumn(['1.35' + suf, self.wlt.walletId, \
                                                   self.wlt.labelName, bType])
         self.scene.moveCursor(15, colRect.y() + colRect.height(), absolute=True)
      else:
         colRect, rowHgt = self.scene.drawColumn(['Wallet Version:', 'Wallet ID:', \
                                                   'Wallet Name:', 'Backup Type:', \
                                                   'Fragment:'])
         baseID = self.fragData['FragIDStr']
         fragNum = printData + 1
         fragID = '<b>%s-<font color="%s">#%d</font></b>' % (baseID, htmlColor('TextBlue'), fragNum)
         self.scene.moveCursor(15, 0)
         suf = 'c' if self.noNeedChaincode else 'a'
         colRect, rowHgt = self.scene.drawColumn(['1.35' + suf, self.wlt.walletId, \
                                                   self.wlt.labelName, bType, fragID])
         self.scene.moveCursor(15, colRect.y() + colRect.height(), absolute=True)


      # Display warning about unprotected key data
      wrap = 0.9 * self.scene.pageRect().width()

      if self.doPrintFrag:
         warnMsg = self.tr(
            'Any subset of <font color="%s"><b>%s</b></font> fragments with this '
            'ID (<font color="%s"><b>%s</b></font>) are sufficient to recover all the '
            'coins contained in this wallet.  To optimize the physical security of '
            'your wallet, please store the fragments in different locations.' % (htmlColor('TextBlue'), \
                           str(self.fragData['M']), htmlColor('TextBlue'), self.fragData['FragIDStr']))
      else:
         container = 'this wallet' if printType == 'SingleSheetFirstPage' else 'these addresses'
         warnMsg = self.tr(
            '<font color="#aa0000"><b>WARNING:</b></font> Anyone who has access to this '
            'page has access to all the bitcoins in %s!  Please keep this '
            'page in a safe place.' % container)

      self.scene.newLine()
      self.scene.drawText(warnMsg, GETFONT('Var', 9), wrapWidth=wrap)

      self.scene.newLine(extra_dy=20)
      self.scene.drawHLine()
      self.scene.newLine(extra_dy=20)

      if self.doPrintFrag:
         numLine = 'three' if self.noNeedChaincode else 'five'
      else:
         numLine = 'two' if self.noNeedChaincode else 'four'

      if printType == 'SingleSheetFirstPage':
         descrMsg = self.tr(
            'The following %s lines backup all addresses '
            '<i>ever generated</i> by this wallet (previous and future). '
            'This can be used to recover your wallet if you forget your passphrase or '
            'suffer hardware failure and lose your wallet files.' % numLine)
      elif printType == 'SingleSheetImported':
         if self.chkSecurePrint.isChecked():
            descrMsg = self.tr(
               'The following is a list of all private keys imported into your '
               'wallet before this backup was made.   These keys are encrypted '
               u'with the SecurePrint\u200b\u2122 code and can only be restored '
               'by entering them into Armory.  Print a copy of this backup without '
               u'the SecurePrint\u200b\u2122 option if you want to be able to import '
               'them into another application.')
         else:
            descrMsg = self.tr(
               'The following is a list of all private keys imported into your '
               'wallet before this backup was made.  Each one must be copied '
               'manually into the application where you wish to import them.')
      elif printType.lower().startswith('frag'):
         fragNum = printData + 1
         descrMsg = self.tr(
            'The following is fragment <font color="%s"><b>#%s</b></font> for this '
            'wallet.' % (htmlColor('TextBlue'), str(printData + 1)))


      self.scene.drawText(descrMsg, GETFONT('var', 8), wrapWidth=wrap)
      self.scene.newLine(extra_dy=10)

      ###########################################################################
      # Draw the SecurePrint box if needed, frag pie, then return cursor
      prevCursor = self.scene.getCursorXY()

      self.lblSecurePrint.setVisible(doMask)
      if doMask:
         self.scene.resetCursor()
         self.scene.moveCursor(4.0 * INCH, 0)
         spWid, spHgt = 2.75 * INCH, 1.5 * INCH,
         if doMask:
            self.scene.drawRect(spWid, spHgt, edgeColor=Colors.Black, penWidth=3)

         self.scene.resetCursor()
         self.scene.moveCursor(4.07 * INCH, 0.07 * INCH)

         self.scene.drawText(self.tr(
            '<b><font color="#770000">CRITICAL:</font>  This backup will not '
            u'work without the SecurePrint\u200b\u2122 '
            'code displayed on the screen during printing. '
            'Copy it here in ink:'), wrapWidth=spWid * 0.93, font=GETFONT('Var', 7))

         self.scene.newLine(extra_dy=8)
         self.scene.moveCursor(4.07 * INCH, 0)
         codeWid, codeHgt = self.scene.drawText('Code:')
         self.scene.moveCursor(0, codeHgt - 3)
         wid = spWid - codeWid
         w, h = self.scene.drawHLine(width=wid * 0.9, penWidth=2)



      # Done drawing other stuff, so return to the original drawing location
      self.scene.moveCursor(*prevCursor, absolute=True)
      ###########################################################################


      ###########################################################################
      # Finally, draw the backup information.

      # If this page is only imported addresses, draw them then bail
      self.bottomOfSceneHeader = self.scene.cursorPos.y()
      if printType == 'SingleSheetImported':
         self.scene.moveCursor(0, 0.1 * INCH)
         importList = self.binImport
         if self.chkSecurePrint.isChecked():
            importList = self.binImportCrypt

         for a160, priv, isCompr in importList[printData[0]:printData[1]]:
            comprByte = ('\x01' if isCompr == 1 else '')
            prprv = encodePrivKeyBase58(priv.toBinStr() + comprByte)
            toPrint = [prprv[i * 6:(i + 1) * 6] for i in range((len(prprv) + 5) / 6)]
            addrHint = '  (%s...)' % hash160_to_addrStr(a160)[:12]
            self.scene.drawText(' '.join(toPrint), GETFONT('Fix', 7))
            self.scene.moveCursor(0.02 * INCH, 0)
            self.scene.drawText(addrHint, GETFONT('Var', 7))
            self.scene.newLine(extra_dy=-3)
            prprv = None
         return


      if self.doPrintFrag:
         M = self.fragData['M']
         Lines = []
         Prefix = []
         fmtrx = self.fragMtrxCrypt if doMask else self.fragMtrx

         try:
            yBin = fmtrx[printData][1]
            binID = base58_to_binary(self.fragData['fragSetID'])
            IDLine = ComputeFragIDLineHex(M, printData, binID, doMask, addSpaces=True)
            if len(yBin) == 32:
               Prefix.append('ID:');  Lines.append(IDLine)
               Prefix.append('F1:');  Lines.append(makeSixteenBytesEasy(yBin[:16 ]))
               Prefix.append('F2:');  Lines.append(makeSixteenBytesEasy(yBin[ 16:]))
            elif len(yBin) == 64:
               Prefix.append('ID:');  Lines.append(IDLine)
               Prefix.append('F1:');  Lines.append(makeSixteenBytesEasy(yBin[:16       ]))
               Prefix.append('F2:');  Lines.append(makeSixteenBytesEasy(yBin[ 16:32    ]))
               Prefix.append('F3:');  Lines.append(makeSixteenBytesEasy(yBin[    32:48 ]))
               Prefix.append('F4:');  Lines.append(makeSixteenBytesEasy(yBin[       48:]))
            else:
               LOGERROR('yBin is not 32 or 64 bytes!  It is %s bytes', len(yBin))
         finally:
            yBin = None

      else:
         # Single-sheet backup
         if doMask:
            code12 = self.backupData.rootEncr
            code34 = self.backupData.chainEncr
         else:
            code12 = self.backupData.rootClear
            code34 = self.backupData.chainClear


         Lines = []
         Prefix = []
         Prefix.append('Root Key:');  Lines.append(code12[0])
         Prefix.append('');           Lines.append(code12[1])
         if not self.noNeedChaincode:
            Prefix.append('Chaincode:'); Lines.append(code34[0])
            Prefix.append('');           Lines.append(code34[1])

      # Draw the prefix
      origX, origY = self.scene.getCursorXY()
      self.scene.moveCursor(20, 0)
      colRect, rowHgt = self.scene.drawColumn(['<b>' + l + '</b>' for l in Prefix])

      nudgeDown = 2  # because the differing font size makes it look unaligned
      self.scene.moveCursor(20, nudgeDown)
      self.scene.drawColumn(Lines,
                              font=GETFONT('Fixed', 8, bold=True), \
                              rowHeight=rowHgt,
                              useHtml=False)

      self.scene.moveCursor(MARGIN, colRect.y() - 2, absolute=True)
      width = self.scene.pageRect().width() - 2 * MARGIN
      self.scene.drawRect(width, colRect.height() + 7, edgeColor=Colors.White, fillColor=None)

      self.scene.newLine(extra_dy=30)
      self.scene.drawText(self.tr(
         'The following QR code is for convenience only.  It contains the '
         'exact same data as the %s lines above.  If you copy this backup '
         'by hand, you can safely ignore this QR code.' % numLine), wrapWidth=4 * INCH)

      self.scene.moveCursor(20, 0)
      x, y = self.scene.getCursorXY()
      edgeRgt = self.scene.pageRect().width() - MARGIN
      edgeBot = self.scene.pageRect().height() - MARGIN

      qrSize = max(1.5 * INCH, min(edgeRgt - x, edgeBot - y, 2.0 * INCH))
      self.scene.drawQR('\n'.join(Lines), qrSize)
      self.scene.newLine(extra_dy=25)

      Lines = None

      # Finally, draw some pie slices at the bottom
      if self.doPrintFrag:
         M, N = self.fragData['M'], self.fragData['N']
         bottomOfPage = self.scene.pageRect().height() + MARGIN
         maxPieHeight = bottomOfPage - self.scene.getCursorXY()[1] - 8
         maxPieWidth = int((self.scene.pageRect().width() - 2 * MARGIN) / N) - 10
         pieSize = min(72., maxPieHeight, maxPieWidth)
         for i in range(N):
            startX, startY = self.scene.getCursorXY()
            drawSize = self.scene.drawPixmapFile('./img/frag%df.png' % M, sizePx=pieSize)
            self.scene.moveCursor(10, 0)
            if i == printData:
               returnX, returnY = self.scene.getCursorXY()
               self.scene.moveCursor(startX, startY, absolute=True)
               self.scene.moveCursor(-5, -5)
               self.scene.drawRect(drawSize[0] + 10, \
                                   drawSize[1] + 10, \
                                   edgeColor=Colors.TextBlue, \
                                   penWidth=3)
               self.scene.newLine()
               self.scene.moveCursor(startX - MARGIN, 0)
               self.scene.drawText('<font color="%s">#%d</font>' % \
                        (htmlColor('TextBlue'), fragNum), GETFONT('Var', 10))
               self.scene.moveCursor(returnX, returnY, absolute=True)


      vbar = self.view.verticalScrollBar()
      vbar.setValue(vbar.minimum())
      self.view.update()

################################################################################
class DlgFragBackup(ArmoryDialog):

   #############################################################################
   def __init__(self, parent, main, wlt):
      super(DlgFragBackup, self).__init__(parent, main)

      self.wlt = wlt

      lblDescrTitle = QRichLabel(self.tr(
         '<b><u>Create M-of-N Fragmented Backup</u> of "%s" (%s)</b>' % (wlt.labelName, wlt.walletId)), doWrap=False)
      lblDescrTitle.setContentsMargins(5, 5, 5, 5)

      self.lblAboveFrags = QRichLabel('')
      self.lblAboveFrags.setContentsMargins(10, 0, 10, 0)

      frmDescr = makeVertFrame([lblDescrTitle, self.lblAboveFrags], \
                                                            STYLE_RAISED)

      self.fragDisplayLastN = 0
      self.fragDisplayLastM = 0

      self.maxM = 5 if not self.main.usermode == USERMODE.Expert else 8
      self.maxN = 6 if not self.main.usermode == USERMODE.Expert else 12
      self.currMinN = 2
      self.maxmaxN = 12

      self.comboM = QtWidgets.QComboBox()
      self.comboN = QtWidgets.QComboBox()

      for M in range(2, self.maxM + 1):
         self.comboM.addItem(str(M))

      for N in range(self.currMinN, self.maxN + 1):
         self.comboN.addItem(str(N))

      self.comboM.setCurrentIndex(1)
      self.comboN.setCurrentIndex(2)

      def updateM():
         self.updateComboN()
         self.createFragDisplay()

      self.comboM.activated.connect(updateM)
      self.comboN.activated.connect(self.createFragDisplay)
      self.comboM.setMinimumWidth(30)
      self.comboN.setMinimumWidth(30)

      btnAccept = QtWidgets.QPushButton(self.tr('Close'))
      btnAccept.clicked.connect(self.accept)
      frmBottomBtn = makeHorizFrame([STRETCH, btnAccept])

      # We will hold all fragments here, in SBD objects.  Destroy all of them
      # before the dialog exits
      self.secureRoot = self.wlt.addrMap['ROOT'].binPrivKey32_Plain.copy()
      self.secureChain = self.wlt.addrMap['ROOT'].chaincode.copy()
      self.secureMtrx = []

      testChain = DeriveChaincodeFromRootKey(self.secureRoot)
      if testChain == self.secureChain:
         self.noNeedChaincode = True
         self.securePrint = self.secureRoot
      else:
         self.securePrint = self.secureRoot + self.secureChain

      self.chkSecurePrint = QtWidgets.QCheckBox(self.tr(u'Use SecurePrint\u200b\u2122 '
         'to prevent exposing keys to printer or other devices'))

      self.scrollArea = QtWidgets.QScrollArea()
      self.createFragDisplay()
      self.scrollArea.setWidgetResizable(True)

      self.ttipSecurePrint = createToolTipWidget(self.tr(
         u'SecurePrint\u200b\u2122 encrypts your backup with a code displayed on '
         'the screen, so that no other devices or processes has access to the '
         'unencrypted private keys (either network devices when printing, or '
         'other applications if you save a fragment to disk or USB device). '
         u'<u>You must keep the SecurePrint\u200b\u2122 code with the backup!</u>'))
      self.lblSecurePrint = QRichLabel(self.tr(
         '<b><font color="%s"><u>IMPORTANT:</u>  You must keep the '
         u'SecurePrint\u200b\u2122 encryption code with your backup! '
         u'Your SecurePrint\u200b\u2122 code is </font> '
         '<font color="%s">%s</font><font color="%s">. '
         'All fragments for a given wallet use the '
         'same code.</font>' % (htmlColor('TextWarn'), htmlColor('TextBlue'), \
         self.backupData.spPass, htmlColor('TextWarn'))))
      self.chkSecurePrint.clicked.connect(self.clickChkSP)
      self.chkSecurePrint.setChecked(False)
      self.lblSecurePrint.setVisible(False)
      frmChkSP = makeHorizFrame([self.chkSecurePrint, self.ttipSecurePrint, STRETCH])

      dlgLayout = QtWidgets.QVBoxLayout()
      dlgLayout.addWidget(frmDescr)
      dlgLayout.addWidget(self.scrollArea)
      dlgLayout.addWidget(frmChkSP)
      dlgLayout.addWidget(self.lblSecurePrint)
      dlgLayout.addWidget(frmBottomBtn)
      setLayoutStretch(dlgLayout, 0, 1, 0, 0, 0)

      self.setLayout(dlgLayout)
      self.setMinimumWidth(650)
      self.setMinimumHeight(450)
      self.setWindowTitle('Create Backup Fragments')


   #############################################################################
   def clickChkSP(self):
      self.lblSecurePrint.setVisible(self.chkSecurePrint.isChecked())
      self.createFragDisplay()


   #############################################################################
   def updateComboN(self):
      M = int(str(self.comboM.currentText()))
      oldN = int(str(self.comboN.currentText()))
      self.currMinN = M
      self.comboN.clear()

      for i, N in enumerate(range(self.currMinN, self.maxN + 1)):
         self.comboN.addItem(str(N))

      if M > oldN:
         self.comboN.setCurrentIndex(0)
      else:
         for i, N in enumerate(range(self.currMinN, self.maxN + 1)):
            if N == oldN:
               self.comboN.setCurrentIndex(i)



   #############################################################################
   def createFragDisplay(self):
      M = int(str(self.comboM.currentText()))
      N = int(str(self.comboN.currentText()))

      #only recompute fragments if M or N changed
      if self.fragDisplayLastN != N or \
         self.fragDisplayLastM != M:
         self.recomputeFragData()

      self.fragDisplayLastN = N
      self.fragDisplayLastM = M

      lblAboveM = QRichLabel(self.tr('<u><b>Required Fragments</b></u> '), hAlign=QtCore.Qt.AlignHCenter, doWrap=False)
      lblAboveN = QRichLabel(self.tr('<u><b>Total Fragments</b></u> '), hAlign=QtCore.Qt.AlignHCenter)
      frmComboM = makeHorizFrame([STRETCH, QtWidgets.QLabel('M:'), self.comboM, STRETCH])
      frmComboN = makeHorizFrame([STRETCH, QtWidgets.QLabel('N:'), self.comboN, STRETCH])

      btnPrintAll = QtWidgets.QPushButton(self.tr('Print All Fragments'))
      btnPrintAll.connect.clicked(self.clickPrintAll)
      leftFrame = makeVertFrame([STRETCH, \
                                 lblAboveM, \
                                 frmComboM, \
                                 lblAboveN, \
                                 frmComboN, \
                                 STRETCH, \
                                 HLINE(), \
                                 btnPrintAll, \
                                 STRETCH], STYLE_STYLED)

      layout = QtWidgets.QHBoxLayout()
      layout.addWidget(leftFrame)

      for f in range(N):
         layout.addWidget(self.createFragFrm(f))


      frmScroll = QtWidgets.QFrame()
      frmScroll.setFrameStyle(STYLE_SUNKEN)
      frmScroll.setStyleSheet('QtWidgets.QFrame { background-color : %s  }' % \
                                                htmlColor('SlightBkgdDark'))
      frmScroll.setLayout(layout)
      self.scrollArea.setWidget(frmScroll)

      BLUE = htmlColor('TextBlue')
      self.lblAboveFrags.setText(self.tr(
         'Any <font color="%s"><b>%d</b></font> of these '
         '<font color="%s"><b>%d</b></font>'
         'fragments are sufficient to restore your wallet, and each fragment '
         'has the ID, <font color="%s"><b>%s</b></font>.  All fragments with the '
         'same fragment ID are compatible with each other!' % (BLUE, M, BLUE, N, BLUE, self.fragPrefixStr)))


   #############################################################################
   def createFragFrm(self, idx):

      doMask = self.chkSecurePrint.isChecked()
      M = int(str(self.comboM.currentText()))
      N = int(str(self.comboN.currentText()))

      lblFragID = QRichLabel(self.tr('<b>Fragment ID:<br>%s-%s</b>' % \
                                    (str(self.fragPrefixStr), str(idx + 1))))
      lblFragPix = QImageLabel(self.fragPixmapFn, size=(72, 72))
      if doMask:
         ys = self.secureMtrxCrypt[idx][1].toBinStr()[:42]
      else:
         ys = self.secureMtrx[idx][1].toBinStr()[:42]

      easyYs1 = makeSixteenBytesEasy(ys[:16   ])
      easyYs2 = makeSixteenBytesEasy(ys[ 16:32])

      binID = base58_to_binary(self.uniqueFragSetID)
      ID = ComputeFragIDLineHex(M, idx, binID, doMask, addSpaces=True)

      fragPreview = 'ID: %s...<br>' % ID[:12]
      fragPreview += 'F1: %s...<br>' % easyYs1[:12]
      fragPreview += 'F2: %s...    ' % easyYs2[:12]
      lblPreview = QRichLabel(fragPreview)
      lblPreview.setFont(GETFONT('Fixed', 9))

      lblFragIdx = QRichLabel('#%d' % (idx + 1), size=4, color='TextBlue', \
                                                   hAlign=QtCore.Qt.AlignHCenter)

      frmTopLeft = makeVertFrame([lblFragID, lblFragIdx, STRETCH])
      frmTopRight = makeVertFrame([lblFragPix, STRETCH])

      frmPaper = makeVertFrame([lblPreview])
      frmPaper.setStyleSheet('QtWidgets.QFrame { background-color : #ffffff  }')

      fnPrint = lambda: self.clickPrintFrag(idx)
      fnSave = lambda: self.clickSaveFrag(idx)

      btnPrintFrag = QtWidgets.QPushButton(self.tr('View/Print'))
      btnSaveFrag = QtWidgets.QPushButton(self.tr('Save to File'))
      btnPrintFrag.clicked.connect(fnPrint)
      btnSaveFrag.clicked.connect(fnSave)
      frmButtons = makeHorizFrame([btnPrintFrag, btnSaveFrag])


      layout = QtWidgets.QGridLayout()
      layout.addWidget(frmTopLeft, 0, 0, 1, 1)
      layout.addWidget(frmTopRight, 0, 1, 1, 1)
      layout.addWidget(frmPaper, 1, 0, 1, 2)
      layout.addWidget(frmButtons, 2, 0, 1, 2)
      layout.setSizeConstraint(QtWidgets.QLayout.SetFixedSize)

      outFrame = QtWidgets.QFrame()
      outFrame.setFrameStyle(STYLE_STYLED)
      outFrame.setLayout(layout)
      return outFrame


   #############################################################################
   def clickPrintAll(self):
      self.clickPrintFrag(range(int(str(self.comboN.currentText()))))

   #############################################################################
   def clickPrintFrag(self, zindex):
      if not isinstance(zindex, (list, tuple)):
         zindex = [zindex]
      fragData = {}
      fragData['M'] = int(str(self.comboM.currentText()))
      fragData['N'] = int(str(self.comboN.currentText()))
      fragData['FragIDStr'] = self.fragPrefixStr
      fragData['FragPixmap'] = self.fragPixmapFn
      fragData['Range'] = zindex
      fragData['Secure'] = self.chkSecurePrint.isChecked()
      fragData['fragSetID'] = self.uniqueFragSetID
      dlg = DlgPrintBackup(self, self.main, self.wlt, 'Fragments', \
                              self.secureMtrx, self.secureMtrxCrypt, fragData, \
                              self.secureRoot, self.secureChain)
      dlg.exec_()

   #############################################################################
   def clickSaveFrag(self, zindex):
      saveMtrx = self.secureMtrx
      doMask = False
      if self.chkSecurePrint.isChecked():
         response = QtWidgets.QMessageBox.question(self, self.tr('Secure Backup?'), self.tr(
            u'You have selected to use SecurePrint\u200b\u2122 for the printed '
            'backups, which can also be applied to fragments saved to file. '
            u'Doing so will require you store the SecurePrint\u200b\u2122 '
            'code with the backup, but it will prevent unencrypted key data from '
            'touching any disks.  <br><br> Do you want to encrypt the fragment '
            u'file with the same SecurePrint\u200b\u2122 code?'), \
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No | QtWidgets.QMessageBox.Cancel)

         if response == QtWidgets.QMessageBox.Yes:
            saveMtrx = self.secureMtrxCrypt
            doMask = True
         elif response == QtWidgets.QMessageBox.No:
            pass
         else:
            return


      wid = self.wlt.walletId
      pref = self.fragPrefixStr
      fnum = zindex + 1
      M = self.M
      sec = 'secure.' if doMask else ''
      defaultFn = 'wallet_%s_%s_num%d_need%d.%sfrag' % (wid, pref, fnum, M, sec)
      #print 'FragFN:', defaultFn
      savepath = self.main.getFileSave('Save Fragment', \
                                       ['Wallet Fragments (*.frag)'], \
                                       defaultFn)

      if len(toUnicode(savepath)) == 0:
         return

      fout = open(savepath, 'w')
      fout.write('Wallet ID:     %s\n' % wid)
      fout.write('Create Date:   %s\n' % unixTimeToFormatStr(RightNow()))
      fout.write('Fragment ID:   %s-#%d\n' % (pref, fnum))
      fout.write('Frag Needed:   %d\n' % M)
      fout.write('\n\n')

      try:
         yBin = saveMtrx[zindex][1].toBinStr()
         binID = base58_to_binary(self.uniqueFragSetID)
         IDLine = ComputeFragIDLineHex(M, zindex, binID, doMask, addSpaces=True)
         if len(yBin) == 32:
            fout.write('ID: ' + IDLine + '\n')
            fout.write('F1: ' + makeSixteenBytesEasy(yBin[:16 ]) + '\n')
            fout.write('F2: ' + makeSixteenBytesEasy(yBin[ 16:]) + '\n')
         elif len(yBin) == 64:
            fout.write('ID: ' + IDLine + '\n')
            fout.write('F1: ' + makeSixteenBytesEasy(yBin[:16       ]) + '\n')
            fout.write('F2: ' + makeSixteenBytesEasy(yBin[ 16:32    ]) + '\n')
            fout.write('F3: ' + makeSixteenBytesEasy(yBin[    32:48 ]) + '\n')
            fout.write('F4: ' + makeSixteenBytesEasy(yBin[       48:]) + '\n')
         else:
            LOGERROR('yBin is not 32 or 64 bytes!  It is %s bytes', len(yBin))
      finally:
         yBin = None

      fout.close()

      qmsg = self.tr(
         'The fragment was successfully saved to the following location: '
         '<br><br> %s <br><br>' % savepath)

      if doMask:
         qmsg += self.tr(
            '<b><u><font color="%s">Important</font</u></b>: '
            'The fragment was encrypted with the '
            u'SecurePrint\u200b\u2122 encryption code.  You must keep this '
            'code with the backup in order to use it!  The code <u>is</u> '
            'case-sensitive! '
            '<br><br> <font color="%s" size=5><b>%s</b></font>'
            '<br><br>'
            'The above code <u><b>is</b></u> case-sensitive!' \
            % (htmlColor('TextWarn'), htmlColor('TextBlue'), \
            self.backupData.spPass))

      QtWidgets.QMessageBox.information(self, self.tr('Success'), qmsg, QtWidgets.QMessageBox.Ok)



   #############################################################################
   def destroyFrags(self):
      if len(self.secureMtrx) == 0:
         return

      if isinstance(self.secureMtrx[0], (list, tuple)):
         for sbdList in self.secureMtrx:
            for sbd in sbdList:
               sbd.destroy()
         for sbdList in self.secureMtrxCrypt:
            for sbd in sbdList:
               sbd.destroy()
      else:
         for sbd in self.secureMtrx:
            sbd.destroy()
         for sbd in self.secureMtrxCrypt:
            sbd.destroy()

      self.secureMtrx = []
      self.secureMtrxCrypt = []


   #############################################################################
   def destroyEverything(self):
      self.secureRoot.destroy()
      self.secureChain.destroy()
      self.securePrint.destroy()
      self.destroyFrags()

   #############################################################################
   def recomputeFragData(self):
      """
      Only M is needed, since N doesn't change
      """

      M = int(str(self.comboM.currentText()))
      N = int(str(self.comboN.currentText()))
      # Make sure only local variables contain non-SBD data
      self.destroyFrags()
      self.uniqueFragSetID = \
         binary_to_base58(SecureBinaryData().GenerateRandom(6).toBinStr())
      insecureData = SplitSecret(self.securePrint, M, self.maxmaxN)
      for x, y in insecureData:
         self.secureMtrx.append([SecureBinaryData(x), SecureBinaryData(y)])
      insecureData, x, y = None, None, None

      #####
      # Now we compute the SecurePrint(TM) versions of the fragments
      SECPRINT = HardcodedKeyMaskParams()
      MASK = lambda x: SECPRINT['FUNC_MASK'](x, ekey=self.binCrypt32)
      if not self.randpass or not self.binCrypt32:
         self.randpass = SECPRINT['FUNC_PWD'](self.secureRoot + self.secureChain)
         self.binCrypt32 = SECPRINT['FUNC_KDF'](self.randpass)
      self.secureMtrxCrypt = []
      for sbdX, sbdY in self.secureMtrx:
         self.secureMtrxCrypt.append([sbdX.copy(), MASK(sbdY)])
      #####

      self.M, self.N = M, N
      self.fragPrefixStr = ComputeFragIDBase58(self.M, \
                              base58_to_binary(self.uniqueFragSetID))
      self.fragPixmapFn = './img/frag%df.png' % M


   #############################################################################
   def accept(self):
      self.destroyEverything()
      super(DlgFragBackup, self).accept()

   #############################################################################
   def reject(self):
      self.destroyEverything()
      super(DlgFragBackup, self).reject()
