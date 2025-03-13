##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2025, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from qtpy import QtCore, QtWidgets

from armoryengine.ArmoryUtils import BIGENDIAN, LITTLEENDIAN, binary_to_hex
from armoryengine.Settings import TheSettings
from armoryengine.AddressUtils import encodePrivKeyBase58

from armorymodels import LedgerDispModelSimple, LedgerDispDelegate, \
   LEDGERCOLS, GETFONT
from qtdialogs.qtdefines import USERMODE, STYLE_RAISED, STYLE_SUNKEN, \
   QRichLabel, HORIZONTAL, VERTICAL, tightSizeStr, tightSizeNChar, \
   makeLayoutFrame, makeHorizFrame, makeVertFrame, QLabelButton, \
   initialColResize, STRETCH, createToolTipWidget
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.QRCodeWidget import QRCodeWidget

##############################################################################
class DlgAddressInfo(ArmoryDialog):
   def __init__(self, wlt, addrObj, parent=None, main=None, mode=None):
      super(DlgAddressInfo, self).__init__(parent, main)

      self.wlt = wlt
      self.addrObj = addrObj
      scrAddr = addrObj.getPrefixedAddr()
      self.ledgerTable = []

      self.mode = mode
      if mode == None:
         if main == None:
            self.mode = USERMODE.Standard
         else:
            self.mode = self.main.usermode

      dlgLayout = QtWidgets.QGridLayout()
      addrStr = addrObj.getAddressString()

      frmInfo = QtWidgets.QFrame()
      frmInfo.setFrameStyle(STYLE_RAISED)
      frmInfoLayout = QtWidgets.QGridLayout()

      lbls = []
      if mode in (USERMODE.Advanced, USERMODE.Expert):
         bin25 = base58_to_binary(addrStr)
         lbls.append([])
         lbls[-1].append(createToolTipWidget(\
            self.tr('This is the computer-readable form of the address')))
         lbls[-1].append(QRichLabel(self.tr('<b>Public Key Hash</b>')))
         h160Str = binary_to_hex(bin25[1:-4])
         if mode == USERMODE.Expert:
            network = binary_to_hex(bin25[:1    ])
            hash160 = binary_to_hex(bin25[ 1:-4 ])
            addrChk = binary_to_hex(bin25[   -4:])
            h160Str += self.tr('%s (Network: %s / Checksum: %s)' % (hash160, network, addrChk))
         lbls[-1].append(QtWidgets.QLabel(h160Str))

      lbls.append([])
      lbls[-1].append(QtWidgets.QLabel(''))
      lbls[-1].append(QRichLabel(self.tr('<b>Wallet:</b>')))
      lbls[-1].append(QtWidgets.QLabel(self.wlt.labelName))

      lbls.append([])
      lbls[-1].append(QtWidgets.QLabel(''))
      lbls[-1].append(QRichLabel(self.tr('<b>Address:</b>')))
      lbls[-1].append(QtWidgets.QLabel(addrStr))

      lbls.append([])
      lbls[-1].append(createToolTipWidget(self.tr(
         'Address type is either <i>Imported</i> or <i>Permanent</i>. '
         '<i>Permanent</i> '
         'addresses are part of the base wallet, and are protected by printed '
         'paper backups, regardless of when the backup was performed. '
         'Imported addresses are only protected by digital backups, or manually '
         'printing the individual keys list, and only if the wallet was backed up '
         '<i>after</i> the keys were imported.')))

      lbls[-1].append(QRichLabel(self.tr('<b>Address Type:</b>')))
      if self.addrObj.chainIndex == -2:
         lbls[-1].append(QtWidgets.QLabel(self.tr('Imported')))
      else:
         lbls[-1].append(QtWidgets.QLabel(self.tr('Permanent')))

      # TODO: fix for BIP-32
      lbls.append([])
      lbls[-1].append(createToolTipWidget(
            self.tr('The index of this address within the wallet.')))
      lbls[-1].append(QRichLabel(self.tr('<b>Index:</b>')))
      if self.addrObj.chainIndex > -1:
         lbls[-1].append(QtWidgets.QLabel(str(self.addrObj.chainIndex+1)))
      else:
         lbls[-1].append(QtWidgets.QLabel(self.tr("Imported")))

      # Current Balance of address
      lbls.append([])
      lbls[-1].append(createToolTipWidget(self.tr(
            'This is the current <i>spendable</i> balance of this address, '
            'not including zero-confirmation transactions from others.')))
      lbls[-1].append(QRichLabel(self.tr('<b>Current Balance</b>')))
      try:
         balCoin = addrObj.getSpendableBalance()
         balStr = coin2str(balCoin, maxZeros=1)
         if balCoin > 0:
            goodColor = htmlColor('MoneyPos')
            lbls[-1].append(QRichLabel(\
               '<font color=' + goodColor + '>' + balStr.strip() + '</font> BTC'))
         else:
            lbls[-1].append(QRichLabel(balStr.strip() + ' BTC'))
      except:
         lbls[-1].append(QRichLabel("N/A"))

      lbls.append([])
      lbls[-1].append(QtWidgets.QLabel(''))
      lbls[-1].append(QRichLabel(self.tr('<b>Comment:</b>')))
      if self.addrObj.chainIndex > -1:
         lbls[-1].append(QtWidgets.QLabel(str(wlt.commentsMap[scrAddr]) if scrAddr in wlt.commentsMap else ''))
      else:
         lbls[-1].append(QtWidgets.QLabel(''))

      lbls.append([])
      lbls[-1].append(createToolTipWidget(
            self.tr('The total number of transactions in which this address was involved')))
      lbls[-1].append(QRichLabel(self.tr('<b>Transaction Count:</b>')))
      #lbls[-1].append(QtWidgets.QLabel(str(len(txHashes))))
      try:
         txnCount = self.addrObj.getTxioCount()
         lbls[-1].append(QtWidgets.QLabel(str(txnCount)))
      except:
         lbls[-1].append(QtWidgets.QLabel("N/A"))

      for i in range(len(lbls)):
         for j in range(1, 3):
            lbls[i][j].setTextInteractionFlags(
               QtCore.Qt.TextSelectableByMouse | QtCore.Qt.TextSelectableByKeyboard)
         for j in range(3):
            if (i, j) == (0, 2):
               frmInfoLayout.addWidget(lbls[i][j], i, j, 1, 2)
            else:
               frmInfoLayout.addWidget(lbls[i][j], i, j, 1, 1)

      qrcode = QRCodeWidget(addrStr, 80, parent=self)
      qrlbl = QRichLabel(self.tr('<font size=2>Double-click to expand</font>'))
      frmqr = makeVertFrame([qrcode, qrlbl])

      frmInfoLayout.addWidget(frmqr, 0, 4, len(lbls), 1)
      frmInfo.setLayout(frmInfoLayout)
      dlgLayout.addWidget(frmInfo, 0, 0, 1, 1)


      # ## Set up the address ledger
      self.ledgerModel = LedgerDispModelSimple(self.ledgerTable, self, self.main)
      delegateId = self.wlt.getLedgerDelegateIdForScrAddr(scrAddr)
      self.ledgerModel.setLedgerDelegateId(delegateId)

      def ledgerToTableScrAddr(ledger):
         return self.main.convertLedgerToTable(ledger, wltIDIn=self.wlt.dbId)
      self.ledgerModel.setConvertLedgerMethod(ledgerToTableScrAddr)

      self.frmLedgUpDown = QtWidgets.QFrame()
      #self.ledgerView = ArmoryTableView(self, self.main, self.frmLedgUpDown)
      self.ledgerView = QtWidgets.QTableView(self.main)
      self.ledgerView.setModel(self.ledgerModel)
      self.ledgerView.setItemDelegate(LedgerDispDelegate(self))

      self.ledgerView.hideColumn(LEDGERCOLS.isOther)
      self.ledgerView.hideColumn(LEDGERCOLS.UnixTime)
      self.ledgerView.hideColumn(LEDGERCOLS.WltID)
      self.ledgerView.hideColumn(LEDGERCOLS.WltName)
      self.ledgerView.hideColumn(LEDGERCOLS.TxHash)
      self.ledgerView.hideColumn(LEDGERCOLS.isCoinbase)
      self.ledgerView.hideColumn(LEDGERCOLS.toSelf)
      self.ledgerView.hideColumn(LEDGERCOLS.optInRBF)

      self.ledgerView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
      self.ledgerView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
      self.ledgerView.horizontalHeader().setStretchLastSection(True)
      self.ledgerView.verticalHeader().setDefaultSectionSize(20)
      self.ledgerView.verticalHeader().hide()
      self.ledgerView.setMinimumWidth(650)

      dateWidth = tightSizeStr(self.ledgerView, '_9999-Dec-99 99:99pm__')[0]
      initialColResize(self.ledgerView, [20, 0, dateWidth, 72, 0, 0.45, 0.3])

      ttipLedger = createToolTipWidget(self.tr(
         'Unlike the wallet-level ledger, this table shows every '
         'transaction <i>input</i> and <i>output</i> as a separate entry. '
         'Therefore, there may be multiple entries for a single transaction, '
         'which will happen if money was sent-to-self (explicitly, or as '
         'the change-back-to-self address).'))
      lblLedger = QtWidgets.QLabel(self.tr('All Address Activity:'))

      lblstrip = makeLayoutFrame(HORIZONTAL, [lblLedger, ttipLedger, STRETCH])
      bottomRow = makeHorizFrame([STRETCH, self.frmLedgUpDown, STRETCH], condenseMargins=True)
      frmLedger = makeLayoutFrame(VERTICAL, [lblstrip, self.ledgerView, bottomRow])
      dlgLayout.addWidget(frmLedger, 1, 0, 1, 1)


      # Now add the right-hand-side option buttons
      lbtnCopyAddr = QLabelButton(self.tr('Copy Address to Clipboard'))
      lbtnViewKeys = QLabelButton(self.tr('View Address Keys'))
      # lbtnSweepA   = QLabelButton('Sweep Address')
      lbtnDelete = QLabelButton(self.tr('Delete Address'))

      lbtnCopyAddr.linkActivated.connect(self.copyAddr)
      lbtnViewKeys.linkActivated.connect(self.viewKeys)
      lbtnDelete.linkActivated.connect(self.deleteAddr)

      optFrame = QtWidgets.QFrame()
      optFrame.setFrameStyle(STYLE_SUNKEN)

      hasPriv = self.addrObj.hasPrivKey
      adv = (self.main.usermode in (USERMODE.Advanced, USERMODE.Expert))
      watch = self.wlt.watchingOnly


      self.lblCopied = QRichLabel('')
      self.lblCopied.setMinimumHeight(tightSizeNChar(self.lblCopied, 1)[1])

      self.lblLedgerWarning = QRichLabel(self.tr(
         'NOTE:  The ledger shows each transaction <i><b>input</b></i> and '
         '<i><b>output</b></i> for this address.  There are typically many '
         'inputs and outputs for each transaction, therefore the entries '
         'represent only partial transactions.  Do not worry if these entries '
         'do not look familiar.'))

      optLayout = QtWidgets.QVBoxLayout()
      if True:           optLayout.addWidget(lbtnCopyAddr)
      if adv:            optLayout.addWidget(lbtnViewKeys)

      if True:           optLayout.addStretch()
      if True:           optLayout.addWidget(self.lblCopied)

      optLayout.addWidget(self.lblLedgerWarning)
      optLayout.addStretch()
      optFrame.setLayout(optLayout)

      rightFrm = makeLayoutFrame(VERTICAL, [QtWidgets.QLabel(self.tr('Available Actions:')), optFrame])
      dlgLayout.addWidget(rightFrm, 0, 1, 2, 1)

      btnGoBack = QtWidgets.QPushButton(self.tr('<<< Go Back'))
      btnGoBack.clicked.connect(self.reject)

      self.setLayout(dlgLayout)
      self.setWindowTitle(self.tr('Address Information'))
      self.ledgerModel.reset()

   def copyAddr(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(self.addrObj.getAddressString())
      self.lblCopied.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      self.lblCopied.setText(self.tr('<i>Copied!</i>'))

   def makePaper(self):
      pass

   def viewKeys(self):
      '''
      if self.wlt.useEncryption and self.wlt.isLocked:
         unlockdlg = DlgUnlockWallet(self.wlt, self, self.main, 'View Private Keys')
         if not unlockdlg.exec_():
            QtWidgets.QMessageBox.critical(self, self.tr('Wallet is Locked'), \
               self.tr('Key information will not include the private key data.'), \
               QtWidgets.QMessageBox.Ok)
      '''
      dlg = DlgShowKeys(self.addrObj, self.wlt, self, self.main)
      dlg.exec_()

   def deleteAddr(self):
      pass

#############################################################################
class DlgShowKeys(ArmoryDialog):
   def __init__(self, addr, wlt, parent=None, main=None):
      super(DlgShowKeys, self).__init__(parent, main)

      self.addr = addr
      self.wlt = wlt
      self.scrAddr = self.addr.getPrefixedAddr()

      lblWarn = QRichLabel('')
      plainPriv = False
      '''
      if addr.binPrivKey32_Plain.getSize() > 0:
         plainPriv = True
         lblWarn = QRichLabel(self.tr(
            '<font color=%s><b>Warning:</b> the unencrypted private keys '
            'for this address are shown below.  They are "private" because '
            'anyone who obtains them can spend the money held '
            'by this address.  Please protect this information the '
            'same as you protect your wallet.</font>' % htmlColor('TextWarn')))
      '''
      warnFrm = makeLayoutFrame(HORIZONTAL, [lblWarn])

      endianness = TheSettings.getSettingOrSetDefault('PrefEndian', BIGENDIAN)
      estr = 'BE' if endianness == BIGENDIAN else 'LE'
      def formatBinData(binStr, endian=LITTLEENDIAN):
         binHex = binary_to_hex(binStr)
         if endian != LITTLEENDIAN:
            binHex = hex_switchEndian(binHex)
         binHexPieces = [binHex[i:i + 8] for i in range(0, len(binHex), 8)]
         return ' '.join(binHexPieces)


      lblDescr = QRichLabel(self.tr(f'Key Data for address: <b>%s</b>' % self.addr.getAddressString()))

      lbls = []

      lbls.append([])
      binKey = b'' #self.addr.binPrivKey32_Plain.toBinStr()
      lbls[-1].append(createToolTipWidget(self.tr(
            'The raw form of the private key for this address.  It is '
            '32-bytes of randomly generated data')))
      lbls[-1].append(QRichLabel(self.tr('Private Key (hex,%s):' % estr)))
      if not addr.hasPrivKey:
         lbls[-1].append(QRichLabel(self.tr('<i>[[ No Private Key in Watching-Only Wallet ]]</i>')))
      elif plainPriv:
         lbls[-1].append(QtWidgets.QLabel(formatBinData(binKey)))
      else:
         lbls[-1].append(QRichLabel(self.tr('<i>[[ ENCRYPTED ]]</i>')))

      if plainPriv:
         lbls.append([])
         lbls[-1].append(createToolTipWidget(self.tr(
               'This is a more compact form of the private key, and includes '
               'a checksum for error detection.')))
         lbls[-1].append(QRichLabel(self.tr('Private Key (Base58):')))
         b58Key = encodePrivKeyBase58(binKey)
         lbls[-1].append(QtWidgets.QLabel(' '.join([b58Key[i:i + 6] for i in range(0, len(b58Key), 6)])))



      lbls.append([])
      lbls[-1].append(createToolTipWidget(self.tr(
               'The raw public key data.  This is the X-coordinate of '
               'the Elliptic-curve public key point.')))
      lbls[-1].append(QRichLabel(self.tr('Public Key X (%s):' % estr)))
      lbls[-1].append(QRichLabel(formatBinData(self.addr.binPublicKey[1:1 + 32])))


      lbls.append([])
      lbls[-1].append(createToolTipWidget(self.tr(
               'The raw public key data.  This is the Y-coordinate of '
               'the Elliptic-curve public key point.')))
      lbls[-1].append(QRichLabel(self.tr('Public Key Y (%s):' % estr)))
      lbls[-1].append(QRichLabel(formatBinData(self.addr.binPublicKey[1 + 32:1 + 32 + 32])))


      bin25 = self.addr.getPrefixedAddr()
      network = binary_to_hex(bin25[:1    ])
      hash160 = binary_to_hex(bin25[ 1:-4 ])
      addrChk = binary_to_hex(bin25[   -4:])
      h160Str = self.tr('%s (Network: %s / Checksum: %s)' % (hash160, network, addrChk))

      lbls.append([])
      lbls[-1].append(createToolTipWidget(\
               self.tr('This is the hexadecimal version if the address string')))
      lbls[-1].append(QRichLabel(self.tr('Public Key Hash:')))
      lbls[-1].append(QtWidgets.QLabel(h160Str))

      frmKeyData = QtWidgets.QFrame()
      frmKeyData.setFrameStyle(STYLE_RAISED)
      frmKeyDataLayout = QtWidgets.QGridLayout()


      # Now set the label properties and jam them into an information frame
      for row, lbl3 in enumerate(lbls):
         lbl3[1].setFont(GETFONT('Var'))
         lbl3[2].setFont(GETFONT('Fixed'))
         lbl3[2].setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                         QtCore.Qt.TextSelectableByKeyboard)
         lbl3[2].setWordWrap(False)

         for j in range(3):
            frmKeyDataLayout.addWidget(lbl3[j], row, j)


      frmKeyData.setLayout(frmKeyDataLayout)

      bbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok)
      bbox.accepted.connect(self.accept)


      dlgLayout = QtWidgets.QVBoxLayout()
      dlgLayout.addWidget(lblWarn)
      dlgLayout.addWidget(lblDescr)
      dlgLayout.addWidget(frmKeyData)
      dlgLayout.addWidget(bbox)


      self.setLayout(dlgLayout)
      self.setWindowTitle(self.tr('Address Key Information'))

