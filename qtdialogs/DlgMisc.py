################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2026, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
from qtpy import QtCore, QtGui, QtWidgets

from armoryengine.ArmoryUtils import USE_TESTNET, USE_REGTEST, \
   LOGINFO, LOGEXCEPT, hex_to_binary
from armoryengine.Transaction import PyTx
from armoryengine.CppBridge import TheBridge

from qtdialogs.qtdefines import tightSizeStr, STRETCH, GETFONT, \
   STYLE_SUNKEN, makeHorizFrame, QRichLabel, tightSizeNChar, \
   relaxedSizeNChar, relaxedSizeStr, HLINE
from qtdialogs.ArmoryDialog import ArmoryDialog
from armorycolors import htmlColor

################################################################################
class DlgECDSACalc(ArmoryDialog):
   def __init__(self, parent=None, main=None, tabStart=0):
      super(DlgECDSACalc, self).__init__(parent, main)

      dispFont = GETFONT('Var', 8)
      w, h = tightSizeNChar(dispFont, 40)

      ##########################################################################
      ##########################################################################
      # TAB:  secp256k1
      ##########################################################################
      ##########################################################################
      # STUB: I'll probably finish implementing this eventually....
      eccWidget = QtWidgets.QWidget()

      tabEccLayout = QtWidgets.QGridLayout()
      self.txtScalarScalarA = QtWidgets.QLineEdit()
      self.txtScalarScalarB = QtWidgets.QLineEdit()
      self.txtScalarScalarC = QtWidgets.QLineEdit()
      self.txtScalarScalarC.setReadOnly(True)

      self.txtScalarPtA = QtWidgets.QLineEdit()
      self.txtScalarPtB_x = QtWidgets.QLineEdit()
      self.txtScalarPtB_y = QtWidgets.QLineEdit()
      self.txtScalarPtC_x = QtWidgets.QLineEdit()
      self.txtScalarPtC_y = QtWidgets.QLineEdit()
      self.txtScalarPtC_x.setReadOnly(True)
      self.txtScalarPtC_y.setReadOnly(True)

      self.txtPtPtA_x = QtWidgets.QLineEdit()
      self.txtPtPtA_y = QtWidgets.QLineEdit()
      self.txtPtPtB_x = QtWidgets.QLineEdit()
      self.txtPtPtB_y = QtWidgets.QLineEdit()
      self.txtPtPtC_x = QtWidgets.QLineEdit()
      self.txtPtPtC_y = QtWidgets.QLineEdit()
      self.txtPtPtC_x.setReadOnly(True)
      self.txtPtPtC_y.setReadOnly(True)

      eccTxtList = [
         self.txtScalarScalarA, self.txtScalarScalarB,
         self.txtScalarScalarC, self.txtScalarPtA, self.txtScalarPtB_x,
         self.txtScalarPtB_y, self.txtScalarPtC_x, self.txtScalarPtC_y,
         self.txtPtPtA_x, self.txtPtPtA_y, self.txtPtPtB_x,
         self.txtPtPtB_y, self.txtPtPtC_x, self.txtPtPtC_y]

      dispFont = GETFONT('Var', 8)
      w, h = tightSizeNChar(dispFont, 60)
      for txt in eccTxtList:
         txt.setMinimumWidth(w)
         txt.setFont(dispFont)

      self.btnCalcSS = QtWidgets.QPushButton(self.tr('Multiply Scalars (mod n)'))
      self.btnCalcSP = QtWidgets.QPushButton(self.tr('Scalar Multiply EC Point'))
      self.btnCalcPP = QtWidgets.QPushButton(self.tr('Add EC Points'))
      self.btnClearSS = QtWidgets.QPushButton(self.tr('Clear'))
      self.btnClearSP = QtWidgets.QPushButton(self.tr('Clear'))
      self.btnClearPP = QtWidgets.QPushButton(self.tr('Clear'))

      imgPlus = QRichLabel('<b>+</b>')
      imgTimes1 = QRichLabel('<b>*</b>')
      imgTimes2 = QRichLabel('<b>*</b>')
      imgDown = QRichLabel('')

      self.btnCalcSS.clicked.connect(self.multss)
      self.btnCalcSP.clicked.connect(self.multsp)
      self.btnCalcPP.clicked.connect(self.addpp)

      ##########################################################################
      # Scalar-Scalar Multiply
      sslblA = QRichLabel('a', hAlign=QtCore.Qt.AlignHCenter)
      sslblB = QRichLabel('b', hAlign=QtCore.Qt.AlignHCenter)
      sslblC = QRichLabel('a*b mod n', hAlign=QtCore.Qt.AlignHCenter)

      ssLayout = QtWidgets.QGridLayout()
      ssLayout.addWidget(sslblA, 0, 0, 1, 1)
      ssLayout.addWidget(sslblB, 0, 2, 1, 1)

      ssLayout.addWidget(self.txtScalarScalarA, 1, 0, 1, 1)
      ssLayout.addWidget(imgTimes1, 1, 1, 1, 1)
      ssLayout.addWidget(self.txtScalarScalarB, 1, 2, 1, 1)

      ssLayout.addWidget(makeHorizFrame(
         [STRETCH, self.btnCalcSS, STRETCH]),
         2, 0, 1, 3)
      ssLayout.addWidget(makeHorizFrame(
         [STRETCH, sslblC, self.txtScalarScalarC, STRETCH]),
         3, 0, 1, 3)
      ssLayout.setVerticalSpacing(1)
      frmSS = QtWidgets.QFrame()
      frmSS.setFrameStyle(STYLE_SUNKEN)
      frmSS.setLayout(ssLayout)

      ##########################################################################
      # Scalar-ECPoint Multiply
      splblA = QRichLabel('a', hAlign=QtCore.Qt.AlignHCenter)
      splblB = QRichLabel('<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      splblBx = QRichLabel('<b>B</b><font size=2>x</font>', hAlign=QtCore.Qt.AlignRight)
      splblBy = QRichLabel('<b>B</b><font size=2>y</font>', hAlign=QtCore.Qt.AlignRight)
      splblC = QRichLabel('<b>C</b> = a*<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      splblCx = QRichLabel('(a*<b>B</b>)<font size=2>x</font>', hAlign=QtCore.Qt.AlignRight)
      splblCy = QRichLabel('(a*<b>B</b>)<font size=2>y</font>', hAlign=QtCore.Qt.AlignRight)
      spLayout = QtWidgets.QGridLayout()
      spLayout.addWidget(splblA, 0, 0, 1, 1)
      spLayout.addWidget(splblB, 0, 2, 1, 1)

      spLayout.addWidget(self.txtScalarPtA, 1, 0, 1, 1)
      spLayout.addWidget(imgTimes2, 1, 1, 1, 1)
      spLayout.addWidget(self.txtScalarPtB_x, 1, 2, 1, 1)
      spLayout.addWidget(self.txtScalarPtB_y, 2, 2, 1, 1)

      spLayout.addWidget(makeHorizFrame(
         [STRETCH, self.btnCalcSP, STRETCH]),
         3, 0, 1, 3)
      spLayout.addWidget(makeHorizFrame(
         [STRETCH, splblCx, self.txtScalarPtC_x, STRETCH]),
         4, 0, 1, 3)
      spLayout.addWidget(makeHorizFrame(
         [STRETCH, splblCy, self.txtScalarPtC_y, STRETCH]),
         5, 0, 1, 3)
      spLayout.setVerticalSpacing(1)
      frmSP = QtWidgets.QFrame()
      frmSP.setFrameStyle(STYLE_SUNKEN)
      frmSP.setLayout(spLayout)

      ##########################################################################
      # ECPoint Addition
      pplblA = QRichLabel('<b>A</b>', hAlign=QtCore.Qt.AlignHCenter)
      pplblB = QRichLabel('<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      pplblAx = QRichLabel('<b>A</b><font size=2>x</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblAy = QRichLabel('<b>A</b><font size=2>y</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblBx = QRichLabel('<b>B</b><font size=2>x</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblBy = QRichLabel('<b>B</b><font size=2>y</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblC = QRichLabel('<b>C</b> = <b>A</b>+<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      pplblCx = QRichLabel('(<b>A</b>+<b>B</b>)<font size=2>x</font>', hAlign=QtCore.Qt.AlignRight)
      pplblCy = QRichLabel('(<b>A</b>+<b>B</b>)<font size=2>y</font>', hAlign=QtCore.Qt.AlignRight)
      ppLayout = QtWidgets.QGridLayout()
      ppLayout.addWidget(pplblA, 0, 0, 1, 1)
      ppLayout.addWidget(pplblB, 0, 2, 1, 1)
      ppLayout.addWidget(self.txtPtPtA_x, 1, 0, 1, 1)
      ppLayout.addWidget(self.txtPtPtA_y, 2, 0, 1, 1)
      ppLayout.addWidget(imgPlus, 1, 1, 2, 1)
      ppLayout.addWidget(self.txtPtPtB_x, 1, 2, 1, 1)
      ppLayout.addWidget(self.txtPtPtB_y, 2, 2, 1, 1)

      ppLayout.addWidget(makeHorizFrame(
         [STRETCH, self.btnCalcPP, STRETCH]),
         3, 0, 1, 3)
      ppLayout.addWidget(makeHorizFrame(
         [STRETCH, pplblCx, self.txtPtPtC_x, STRETCH]),
         4, 0, 1, 3)
      ppLayout.addWidget(makeHorizFrame(
         [STRETCH, pplblCy, self.txtPtPtC_y, STRETCH]),
         5, 0, 1, 3)
      ppLayout.setVerticalSpacing(1)
      frmPP = QtWidgets.QFrame()
      frmPP.setFrameStyle(STYLE_SUNKEN)
      frmPP.setLayout(ppLayout)

      gxstr = '79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798'
      gystr = '483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8'

      lblDescr = QRichLabel(self.tr(
         'Use this form to perform Bitcoin elliptic curve calculations.  All '
         'operations are performed on the secp256k1 elliptic curve, which is '
         'the one used for Bitcoin. '
         'Supply all values as 32-byte, big-endian, hex-encoded integers. '
         '<br><br>'
         'The following is the secp256k1 generator point coordinates (G): <br> '
         '<b>G</b><sub>x</sub>: %s <br> '
         '<b>G</b><sub>y</sub>: %s' % (gxstr, gystr)))

      lblDescr.setTextInteractionFlags(
         QtCore.Qt.TextSelectableByMouse | QtCore.Qt.TextSelectableByKeyboard)

      btnClear = QtWidgets.QPushButton(self.tr('Clear'))
      btnClear.setMaximumWidth(2 * relaxedSizeStr(btnClear, 'Clear')[0])
      btnClear.clicked.connect(self.eccClear)

      btnBack = QtWidgets.QPushButton(self.tr('<<< Go Back'))
      btnBack.clicked.connect(self.accept)
      frmBack = makeHorizFrame([btnBack, STRETCH])

      eccLayout = QtWidgets.QVBoxLayout()
      eccLayout.addWidget(makeHorizFrame([lblDescr, btnClear]))
      eccLayout.addWidget(frmSS)
      eccLayout.addWidget(frmSP)
      eccLayout.addWidget(frmBack)
      eccWidget.setLayout(eccLayout)

      calcLayout = QtWidgets.QHBoxLayout()
      calcLayout.addWidget(eccWidget)
      self.setLayout(calcLayout)

      self.setWindowTitle(self.tr('ECDSA Calculator'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))

   #############################################################################
   def getBinary(self, widget, name):
      try:
         hexVal = str(widget.text())
         binVal = hex_to_binary(hexVal)
      except:
         QtWidgets.QMessageBox.critical(self,
            self.tr('Bad Input'),
            self.tr(
               f"Value \"{name}\" is invalid. "
               "Make sure the value is specified in hex, big-endian."),
            QtWidgets.QMessageBox.Ok)
         return ''
      return binVal

   ########
   def multss(self):
      binA = self.getBinary(self.txtScalarScalarA, 'a')
      binB = self.getBinary(self.txtScalarScalarB, 'b')
      C = CryptoECDSA().ECMultiplyScalars(binA, binB)
      self.txtScalarScalarC.setText(binary_to_hex(C))

      for txt in [
         self.txtScalarScalarA,
         self.txtScalarScalarB,
         self.txtScalarScalarC]:
         txt.setCursorPosition(0)

   def multsp(self):
      binA = self.getBinary(self.txtScalarPtA, 'a')
      binBx = self.getBinary(self.txtScalarPtB_x, '<b>B</b><font size=2>x</font>')
      binBy = self.getBinary(self.txtScalarPtB_y, '<b>B</b><font size=2>y</font>')

      if not CryptoECDSA().ECVerifyPoint(binBx, binBy):
         QtWidgets.QMessageBox.critical(self,
            self.tr('Invalid EC Point'),
            self.tr('The point you specified (<b>B</b>) is not on the '
               'elliptic curve used in Bitcoin (secp256k1).'),
            QtWidgets.QMessageBox.Ok)
         return

      C = CryptoECDSA().ECMultiplyPoint(binA, binBx, binBy)
      self.txtScalarPtC_x.setText(binary_to_hex(C[:32]))
      self.txtScalarPtC_y.setText(binary_to_hex(C[32:]))

      for txt in [
         self.txtScalarPtA,
         self.txtScalarPtB_x, self.txtScalarPtB_y,
         self.txtScalarPtC_x, self.txtScalarPtC_y]:
         txt.setCursorPosition(0)

   def addpp(self):
      binAx = self.getBinary(self.txtPtPtA_x, '<b>A</b><font size=2>x</font>')
      binAy = self.getBinary(self.txtPtPtA_y, '<b>A</b><font size=2>y</font>')
      binBx = self.getBinary(self.txtPtPtB_x, '<b>B</b><font size=2>x</font>')
      binBy = self.getBinary(self.txtPtPtB_y, '<b>B</b><font size=2>y</font>')

      if not CryptoECDSA().ECVerifyPoint(binAx, binAy):
         QtWidgets.QMessageBox.critical(self,
            self.tr('Invalid EC Point'), \
            self.tr('The point you specified (<b>A</b>) is not on the '
               'elliptic curve used in Bitcoin (secp256k1).'),
            QtWidgets.QMessageBox.Ok)
         return

      if not CryptoECDSA().ECVerifyPoint(binBx, binBy):
         QtWidgets.QMessageBox.critical(self,
            self.tr('Invalid EC Point'),
            self.tr('The point you specified (<b>B</b>) is not on the '
               'elliptic curve used in Bitcoin (secp256k1).'),
            QtWidgets.QMessageBox.Ok)
         return

      C = CryptoECDSA().ECAddPoints(binAx, binAy, binBx, binBy)
      self.txtPtPtC_x.setText(binary_to_hex(C[:32]))
      self.txtPtPtC_y.setText(binary_to_hex(C[32:]))

      for txt in [
         self.txtPtPtA_x, self.txtPtPtA_y,
         self.txtPtPtB_x, self.txtPtPtB_y,
         self.txtPtPtC_x, self.txtPtPtC_y]:
         txt.setCursorPosition(0)

   def eccClear(self):
      self.txtScalarScalarA.setText('')
      self.txtScalarScalarB.setText('')
      self.txtScalarScalarC.setText('')

      self.txtScalarPtA.setText('')
      self.txtScalarPtB_x.setText('')
      self.txtScalarPtB_y.setText('')
      self.txtScalarPtC_x.setText('')
      self.txtScalarPtC_y.setText('')

      self.txtPtPtA_x.setText('')
      self.txtPtPtA_y.setText('')
      self.txtPtPtB_x.setText('')
      self.txtPtPtB_y.setText('')
      self.txtPtPtC_x.setText('')
      self.txtPtPtC_y.setText('')

################################################################################
class DlgBroadcastBlindTx(ArmoryDialog):
   def __init__(self, main=None, parent=None):
      super(DlgBroadcastBlindTx, self).__init__(parent, main)

      self.pytx = None
      self.txList = []

      lblDescr = QRichLabel(self.tr(
         'Copy a raw, hex-encoded transaction below to have Armory '
         'broadcast it to the Bitcoin network.  This function is '
         'provided as a convenience to expert users, and carries '
         'no guarantees of usefulness. '
         '<br><br>'
         'Specifically, be aware of the following limitations of '
         'this broadcast function: '
         '<ul>'
         '<li>The transaction will be "broadcast" by sending it '
         'to the connected Bitcon Core instance which will '
         'forward it to the rest of the Bitcoin network. '
         'However, if the transaction is non-standard or '
         'does not satisfy standard fee rules, Bitcoin Core '
         '<u>will</u> drop it and it '
         'will never be seen by the Bitcoin network. '
         '</li>'
         '<li>There will be no feedback as to whether the '
         'transaction succeeded.  You will have to verify the '
         'success of this operation via other means. '
         'However, if the transaction sends '
         'funds directly to or from an address in one of your '
         'wallets, it will still generate a notification and show '
         'up in your transaction history for that wallet. '
         '</li>'
         '</ul>'))

      self.txtRawTx = QtWidgets.QPlainTextEdit()
      self.txtRawTx.setFont(GETFONT('Fixed', 9))
      w,h = relaxedSizeNChar(self.txtRawTx, 90)
      self.txtRawTx.setMinimumWidth(w)
      self.txtRawTx.setMinimumHeight(h*5)
      self.txtRawTx.textChanged.connect(self.txChanged)

      lblTxInfo = QRichLabel(self.tr('Parsed Transaction:'))

      self.txtTxInfo = QtWidgets.QPlainTextEdit()
      self.txtTxInfo.setFont(GETFONT('Fixed', 9))
      self.txtTxInfo.setMinimumWidth(w)
      self.txtTxInfo.setMinimumHeight(h*7)
      self.txtTxInfo.setReadOnly(True)

      self.lblInvalid = QRichLabel('')

      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAdd    = QtWidgets.QPushButton(self.tr("Add"))
      self.btnBroad  = QtWidgets.QPushButton(self.tr("Broadcast"))
      self.btnBroad.setEnabled(False)
      self.btnCancel.clicked.connect(self.reject)
      self.btnAdd.clicked.connect(self.addTx)
      self.btnBroad.clicked.connect(self.doBroadcast)
      frmButtons = makeHorizFrame(
         ['Stretch', self.btnCancel, self.btnAdd, self.btnBroad])

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(self.txtRawTx)
      layout.addWidget(lblTxInfo)
      layout.addWidget(self.txtTxInfo)
      layout.addWidget(self.lblInvalid)
      layout.addWidget(HLINE())
      layout.addWidget(frmButtons)

      self.setLayout(layout)
      self.setWindowTitle(self.tr("Broadcast Raw Transaction"))

   #############################################################################
   def txChanged(self):
      try:
         txt = str(self.txtRawTx.toPlainText()).strip()
         txt = ''.join(txt.split())  # removes all whitespace
         if txt:
            self.pytx = PyTx().unserialize(hex_to_binary(txt))
            self.txtTxInfo.setPlainText(self.pytx.toString())
            LOGINFO('Valid tx entered:')
            LOGINFO(self.pytx.toString())
            self.setReady(True)
            return
      except Exception as e:
         LOGEXCEPT(f'Failed to parse tx: {str(e)}')
      self.setReady(False)
      self.pytx = None

   #############################################################################
   def addTx(self):
      #TODO: add a tab per loaded tx. There ought to be some feedback...
      if self.pytx is not None:
         self.txList.append(self.pytx)
      self.txtRawTx.clear()

   #############################################################################
   def setReady(self, isTrue):
      self.btnBroad.setEnabled(isTrue)
      self.lblInvalid.setText("")
      if not isTrue:
         self.txtTxInfo.setPlainText('')
         if len(str(self.txtRawTx.toPlainText()).strip()) > 0:
            self.lblInvalid.setText(self.tr('<font color="%s"><b>Raw transaction '
            'is invalid!</font></b>' % htmlColor('TextWarn')))

   #############################################################################
   def doBroadcast(self):
      if self.pytx is not None:
         self.txList.append(self.pytx)

      rawTxList = []
      for txObj in self.txList:
         rawTxList.append(txObj.serialize())
      TheBridge.service.broadcastTx(rawTxList)
      self.accept()

      #TODO: there should be some sort of MsgBox informing of result
      '''
      txhash = self.pytx.getHash()


      hexhash = binary_to_hex(txhash, endOut=BIGENDIAN)
      if USE_TESTNET:
         linkToExplorer = 'https://blockexplorer.com/testnet/tx/%s' % hexhash
         dispToExplorer = 'https://blockexplorer.com/testnet/tx/%s...' % hexhash[:16]
      elif USE_REGTEST:
         linkToExplorer = ''
         dispToExplorer = ''
      else:
         linkToExplorer = 'https://blockchain.info/search/%s' % hexhash
         dispToExplorer = 'https://blockchain.info/search/%s...' % hexhash[:16]

      QtWidgets.QMessageBox.information(self, self.tr("Broadcast!"), self.tr(
         'Your transaction was successfully sent to the local Bitcoin '
         'Core instance, though there is no guarantees that it was '
         'forwarded to the rest of the network.   On testnet, just about '
         'every valid transaction will successfully propagate.  On the '
         'main Bitcoin network, this will fail unless it was a standard '
         'transaction type. '
         'The transaction '
         'had the following hash: '
         '<br><br> '
         '%1 '
         '<br><br>'
         'You can check whether it was seen by other nodes on the network '
         'with the link below: '
         '<br><br>'
         '<a href="%2">%3</a>').arg(hexhash, linkToExplorer, dispToExplorer), QtWidgets.QMessageBox.Ok)
      '''
