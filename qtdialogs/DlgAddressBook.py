##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2024, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from qtpy import QtCore, QtGui, QtWidgets

from armorymodels import AllWalletsDispModel, WLTVIEWCOLS, \
   SentToAddrBookModel, SentAddrSortProxy, ADDRBOOKCOLS, \
   WalletAddrDispModel, WalletAddrSortProxy, ADDRESSCOLS
from armoryengine.ArmoryUtils import DEFAULT_RECEIVE_TYPE, P2SHBYTE
from armoryengine.AddressUtils import addrStr_to_hash160
from armoryengine.MultiSigUtils import isBareLockbox, isP2SHLockbox
from armoryengine.Settings import TheSettings

from qtdialogs.qtdefines import QRichLabel, tightSizeStr, STRETCH, \
   initialColResize, USERMODE, HLINE, makeHorizFrame, restoreTableView, \
   saveTableView, createToolTipWidget
from qtdialogs.DlgSetComment import DlgSetComment
from qtdialogs.ArmoryDialog import ArmoryDialog

from ui.MultiSigModels import LockboxDisplayModel, LockboxDisplayProxy, \
   LOCKBOXCOLS

################################################################################
def createAddrBookButton(parent, targWidget, defaultWltID=None, actionStr="Select",
                         selectExistingOnly=False, selectMineOnly=False, getPubKey=False,
                         showLockboxes=True):
   action = parent.tr("Select")
   btn = QtWidgets.QPushButton('')
   ico = QtGui.QIcon(QtGui.QPixmap('./img/addr_book_icon.png'))
   btn.setIcon(ico)
   def execAddrBook():
      if len(parent.main.walletMap) == 0:
         QtWidgets.QMessageBox.warning(parent, parent.tr('No wallets!'), parent.tr('You have no wallets so '
            'there is no address book to display.'), QtWidgets.QMessageBox.Ok)
         return
      dlg = DlgAddressBook(parent, parent.main, targWidget, defaultWltID,
                    action, selectExistingOnly, selectMineOnly, getPubKey,
                           showLockboxes)
      dlg.exec_()

   btn.setMaximumWidth(24)
   btn.setMaximumHeight(24)
   btn.clicked.connect(execAddrBook)
   btn.setToolTip(parent.tr('Select from Address Book'))
   return btn

################################################################################
class DlgAddressBook(ArmoryDialog):
   """
   This dialog is provided a widget which has a "setText()" method.  When the
   user selects the address, this dialog will enter the text into the widget
   and then close itself.
   """
   def __init__(self, parent, main, putResultInWidget=None, \
                                    defaultWltID=None, \
                                    actionStr='Select', \
                                    selectExistingOnly=False, \
                                    selectMineOnly=False, \
                                    getPubKey=False,
                                    showLockboxes=True):
      super(DlgAddressBook, self).__init__(parent, main)

      self.target = putResultInWidget
      self.actStr = self.tr('Select')
      self.returnPubKey = getPubKey

      self.isBrowsingOnly = (self.target == None)

      if defaultWltID == None:
         defaultWltID = self.main.walletIDList[0]

      wltObj = self.main.walletMap[defaultWltID]

      lblDescr = QRichLabel(self.tr('Choose an address from your transaction history, '
                            'or your own wallet.  If you choose to send to one '
                            'of your own wallets, the next unused address in '
                            'that wallet will be used.'))

      if self.isBrowsingOnly or selectExistingOnly:
         lblDescr = QRichLabel(self.tr('Browse all receiving addresses in '
                               'this wallet, and all addresses to which this '
                               'wallet has sent bitcoins.'))

      lblToWlt = QRichLabel(self.tr('<b>Send to Wallet:</b>'))
      lblToAddr = QRichLabel(self.tr('<b>Send to Address:</b>'))
      if self.isBrowsingOnly:
         lblToWlt.setVisible(False)
         lblToAddr.setVisible(False)


      rowHeight = tightSizeStr(self.font(), 'XygjpHI')[1]

      self.wltDispModel = AllWalletsDispModel(self.main)
      self.wltDispView = QtWidgets.QTableView()
      self.wltDispView.setModel(self.wltDispModel)
      self.wltDispView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
      self.wltDispView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
      self.wltDispView.horizontalHeader().setStretchLastSection(True)
      self.wltDispView.verticalHeader().setDefaultSectionSize(20)
      self.wltDispView.setMaximumHeight(rowHeight * 7.7)
      self.wltDispView.hideColumn(WLTVIEWCOLS.Visible)
      initialColResize(self.wltDispView, [0.15, 0.30, 0.2, 0.20])
      self.wltDispView.selectionModel().currentChanged.connect(\
         self.wltTableClicked)

      def toggleAddrType(addrtype):
         self.addrType = addrtype
         self.wltTableClicked(self.wltDispView.selectionModel().currentIndex())

      from ui.AddressTypeSelectDialog import AddressLabelFrame
      self.addrType = wltObj.getDefaultAddressType()
      self.addrTypeSelectFrame = AddressLabelFrame(self, \
         toggleAddrType, wltObj.getAddressTypes(), self.addrType)

      # DISPLAY sent-to addresses
      self.addrBookTxModel = None
      self.addrBookTxView = QtWidgets.QTableView()
      self.addrBookTxView.setSortingEnabled(True)
      self.addrBookTxView.doubleClicked.connect(self.dblClickAddressTx)
      self.addrBookTxView.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
      self.addrBookTxView.customContextMenuRequested.connect(self.showContextMenuTx)

      # DISPLAY receiving addresses
      self.addrBookRxModel = None
      self.addrBookRxView = QtWidgets.QTableView()
      self.addrBookRxView.setSortingEnabled(True)
      self.addrBookRxView.doubleClicked.connect(self.dblClickAddressRx)

      self.addrBookRxView.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
      self.addrBookRxView.customContextMenuRequested.connect(self.showContextMenuRx)


      self.tabWidget = QtWidgets.QTabWidget()
      self.tabWidget.addTab(self.addrBookRxView, self.tr('Receiving (Mine)'))
      if not selectMineOnly:
         self.tabWidget.addTab(self.addrBookTxView, self.tr('Sending (Other\'s)'))
      # DISPLAY Lockboxes - Regardles off what showLockboxes says only show
      # Lockboxes in Expert mode
      if showLockboxes and self.main.usermode == USERMODE.Expert:
         self.lboxModel = LockboxDisplayModel(self.main, \
                                    self.main.allLockboxes, \
                                    self.main.getPreferredDateFormat())
         self.lboxProxy = LockboxDisplayProxy(self)
         self.lboxProxy.setSourceModel(self.lboxModel)
         self.lboxProxy.sort(LOCKBOXCOLS.CreateDate, QtCore.Qt.DescendingOrder)
         self.lboxView = QtWidgets.QTableView()
         self.lboxView.setModel(self.lboxProxy)
         self.lboxView.setSortingEnabled(True)
         self.lboxView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
         self.lboxView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
         self.lboxView.verticalHeader().setDefaultSectionSize(18)
         self.lboxView.horizontalHeader().setStretchLastSection(True)
         #maxKeys = max([lb.N for lb in self.main.allLockboxes])
         for i in range(LOCKBOXCOLS.Key0, LOCKBOXCOLS.Key4+1):
            self.lboxView.hideColumn(i)
         self.lboxView.hideColumn(LOCKBOXCOLS.UnixTime)
         self.tabWidget.addTab(self.lboxView, 'Lockboxes')
         self.lboxView.doubleClicked.connect(self.dblClickedLockbox)
         self.lboxView.selectionModel().currentChanged.connect(\
            self.clickedLockbox)
      else:
         self.lboxView = None
      self.tabWidget.currentChanged.connect(self.tabChanged)
      self.tabWidget.setCurrentIndex(0)



      ttipSendWlt = createToolTipWidget(\
         self.tr('The next unused address in that wallet will be calculated and selected. '))
      ttipSendAddr = createToolTipWidget(\
         self.tr('Addresses that are in other wallets you own are <b>not showns</b>.'))


      self.lblSelectWlt = QRichLabel('', doWrap=False)
      self.btnSelectWlt = QtWidgets.QPushButton(self.tr('No Wallet Selected'))
      self.useBareMultiSigCheckBox = QtWidgets.QCheckBox(self.tr('Use Bare Multi-Sig (No P2SH)'))
      self.useBareMultiSigCheckBox.setVisible(False)
      self.ttipBareMS = createToolTipWidget( self.tr(
         'EXPERT OPTION:  Do not check this box unless you know what it means '
                         'and you need it!  Forces Armory to exposes public '
                         'keys to the blockchain before the funds are spent. '
                         'This is only needed for very specific use cases, '
                         'and otherwise creates blockchain bloat.'))


      self.ttipBareMS.setVisible(False)
      self.btnSelectAddr = QtWidgets.QPushButton(self.tr('No Address Selected'))
      self.btnSelectWlt.setEnabled(False)
      self.btnSelectAddr.setEnabled(False)
      btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))

      if self.isBrowsingOnly:
         self.btnSelectWlt.setVisible(False)
         self.btnSelectAddr.setVisible(False)
         self.lblSelectWlt.setVisible(False)
         btnCancel = QtWidgets.QPushButton(self.tr('<<< Go Back'))
         ttipSendAddr.setVisible(False)

      if selectExistingOnly:
         lblToWlt.setVisible(False)
         self.lblSelectWlt.setVisible(False)
         self.btnSelectWlt.setVisible(False)
         ttipSendWlt.setVisible(False)

      self.btnSelectWlt.clicked.connect(self.acceptWltSelection)
      self.btnSelectAddr.clicked.connect(self.acceptAddrSelection)
      self.useBareMultiSigCheckBox.clicked.connect(self.useP2SHClicked)
      btnCancel.clicked.connect(self.reject)


      dlgLayout = QtWidgets.QGridLayout()
      dlgLayout.addWidget(lblDescr, 0, 0)
      dlgLayout.addWidget(HLINE(), 1, 0)
      dlgLayout.addWidget(lblToWlt, 2, 0)
      dlgLayout.addWidget(self.wltDispView, 3, 0)
      dlgLayout.addWidget(makeHorizFrame([self.lblSelectWlt, \
                                          self.addrTypeSelectFrame.getFrame(), \
                                          self.btnSelectWlt]), 4, 0)
      dlgLayout.addWidget(HLINE(), 6, 0)
      dlgLayout.addWidget(lblToAddr, 7, 0)
      dlgLayout.addWidget(self.tabWidget, 8, 0)
      dlgLayout.addWidget(makeHorizFrame([STRETCH, self.useBareMultiSigCheckBox, self.ttipBareMS, self.btnSelectAddr]), 9, 0)
      dlgLayout.addWidget(HLINE(), 10, 0)
      dlgLayout.addWidget(makeHorizFrame([btnCancel, STRETCH]), 11, 0)
      dlgLayout.setRowStretch(3, 1)
      dlgLayout.setRowStretch(8, 2)

      self.setLayout(dlgLayout)
      self.sizeHint = lambda: QtCore.QSize(760, 500)

      # Auto-select the default wallet, if there is one
      rowNum = 0
      if defaultWltID and defaultWltID in self.main.walletMap:
         rowNum = self.main.walletIndices[defaultWltID]
      rowIndex = self.wltDispModel.index(rowNum, 0)
      self.wltDispView.setCurrentIndex(rowIndex)

      self.setWindowTitle('Address Book')
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))

      self.setMinimumWidth(300)

      hexgeom = TheSettings.get('AddrBookGeometry')
      wltgeom = TheSettings.get('AddrBookWltTbl')
      rxgeom = TheSettings.get('AddrBookRxTbl')
      txgeom = TheSettings.get('AddrBookTxTbl')
      if len(hexgeom) > 0:
         if type(hexgeom) == str:
            geom = QtCore.QByteArray(bytes.fromhex(hexgeom))
         else:
            geom = hexgeom
         self.restoreGeometry(geom)
      if len(wltgeom) > 0:
         restoreTableView(self.wltDispView, wltgeom)
      if len(rxgeom) > 0:
         restoreTableView(self.addrBookRxView, rxgeom)
      if len(txgeom) > 0 and not selectMineOnly:
         restoreTableView(self.addrBookTxView, txgeom)

   #############################################################################
   def saveGeometrySettings(self):
      TheSettings.set('AddrBookGeometry', self.saveGeometry().toHex())
      TheSettings.set('AddrBookWltTbl', saveTableView(self.wltDispView))
      TheSettings.set('AddrBookRxTbl', saveTableView(self.addrBookRxView))
      TheSettings.set('AddrBookTxTbl', saveTableView(self.addrBookTxView))

   #############################################################################
   def closeEvent(self, event):
      self.saveGeometrySettings()
      super(DlgAddressBook, self).closeEvent(event)

   #############################################################################
   def accept(self, *args):
      self.saveGeometrySettings()
      super(DlgAddressBook, self).accept(*args)

   #############################################################################
   def reject(self, *args):
      self.saveGeometrySettings()
      super(DlgAddressBook, self).reject(*args)

   #############################################################################
   def setAddrBookTxModel(self, wltID):
      self.addrBookTxModel = SentToAddrBookModel(wltID, self.main)

      #
      self.addrBookTxProxy = SentAddrSortProxy(self)
      self.addrBookTxProxy.setSourceModel(self.addrBookTxModel)
      # self.addrBookTxProxy.sort(ADDRBOOKCOLS.Address)

      self.addrBookTxView.setModel(self.addrBookTxProxy)
      self.addrBookTxView.setSortingEnabled(True)
      self.addrBookTxView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
      self.addrBookTxView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
      self.addrBookTxView.horizontalHeader().setStretchLastSection(True)
      self.addrBookTxView.verticalHeader().setDefaultSectionSize(20)
      freqSize = 1.3 * tightSizeStr(self.addrBookTxView, 'Times Used')[0]
      initialColResize(self.addrBookTxView, [0.3, 0.1, freqSize, 0.5])
      self.addrBookTxView.hideColumn(ADDRBOOKCOLS.WltID)
      self.addrBookTxView.selectionModel().currentChanged.connect(\
         self.addrTableTxClicked)

   #############################################################################
   def disableSelectButtons(self):
      self.btnSelectAddr.setText(self.tr('None Selected'))
      self.btnSelectAddr.setEnabled(False)
      self.useBareMultiSigCheckBox.setChecked(False)
      self.useBareMultiSigCheckBox.setEnabled(False)


   #############################################################################
   # Update the controls when the tab changes
   def tabChanged(self, index):
      if not self.isBrowsingOnly:
         if self.tabWidget.currentWidget() == self.lboxView:
            self.useBareMultiSigCheckBox.setVisible(self.btnSelectAddr.isVisible())
            self.ttipBareMS.setVisible(self.btnSelectAddr.isVisible())
            selectedLockBox = self.getSelectedLBID()
            self.btnSelectAddr.setEnabled(selectedLockBox != None)
            if selectedLockBox:
               self.btnSelectAddr.setText( createLockboxEntryStr(selectedLockBox,
                                                                 self.useBareMultiSigCheckBox.isChecked()))
               self.useBareMultiSigCheckBox.setEnabled(True)
            else:
               self.disableSelectButtons()
         elif self.tabWidget.currentWidget() == self.addrBookTxView:
            self.useBareMultiSigCheckBox.setVisible(False)
            self.ttipBareMS.setVisible(False)
            selection = self.addrBookTxView.selectedIndexes()
            if len(selection)==0:
               self.disableSelectButtons()
            else:
               self.addrTableTxClicked(selection[0])
         elif self.tabWidget.currentWidget() == self.addrBookRxView:
            self.useBareMultiSigCheckBox.setVisible(False)
            self.ttipBareMS.setVisible(False)
            selection = self.addrBookRxView.selectedIndexes()
            if len(selection)==0:
               self.disableSelectButtons()
            else:
               self.addrTableRxClicked(selection[0])


   #############################################################################
   def setAddrBookRxModel(self, wltID):
      wlt = self.main.walletMap[wltID]
      self.addrBookRxModel = WalletAddrDispModel(wlt, self)

      self.addrBookRxProxy = WalletAddrSortProxy(self)
      self.addrBookRxProxy.setSourceModel(self.addrBookRxModel)
      # self.addrBookRxProxy.sort(ADDRESSCOLS.Address)

      self.addrBookRxView.setModel(self.addrBookRxProxy)
      self.addrBookRxView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
      self.addrBookRxView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
      self.addrBookRxView.horizontalHeader().setStretchLastSection(True)
      self.addrBookRxView.verticalHeader().setDefaultSectionSize(20)
      iWidth = tightSizeStr(self.addrBookRxView, 'Imp')[0]
      initialColResize(self.addrBookRxView, [iWidth * 1.3, 0.3, 0.35, 64, 0.3])
      self.addrBookRxView.selectionModel().currentChanged.connect(\
         self.addrTableRxClicked)

   #############################################################################
   def wltTableClicked(self, currIndex, prevIndex=None):
      if prevIndex == currIndex:
         return

      self.btnSelectWlt.setEnabled(True)
      row = currIndex.row()
      self.selectedWltID = currIndex.model().index(row, WLTVIEWCOLS.ID).data()

      self.setAddrBookTxModel(self.selectedWltID)
      self.setAddrBookRxModel(self.selectedWltID)

      if not self.isBrowsingOnly:
         wlt = self.main.walletMap[self.selectedWltID]
         self.btnSelectWlt.setText(self.tr('%s Wallet: %s' % (self.actStr, self.selectedWltID)))

         # If switched wallet selection, de-select address so it doesn't look
         # like the currently-selected address is for this different wallet
         if not self.tabWidget.currentWidget() == self.lboxView:
            self.disableSelectButtons()
            self.selectedAddr = ''
            self.selectedCmmt = ''
      self.addrBookTxModel.reset()

      #update address type frame
      wltObj = self.main.walletMap[self.selectedWltID]
      self.addrType = wltObj.getDefaultAddressType()
      self.addrTypeSelectFrame.updateAddressTypes(
         wltObj.getAddressTypes(), self.addrType)

   #############################################################################
   def addrTableTxClicked(self, currIndex, prevIndex=None):
      if prevIndex == currIndex:
         return

      self.btnSelectAddr.setEnabled(True)
      row = currIndex.row()
      self.selectedAddr = currIndex.model().index(\
         row, ADDRBOOKCOLS.Address).data()
      self.selectedCmmt = currIndex.model().index(\
         row, ADDRBOOKCOLS.Comment).data()

      if not self.isBrowsingOnly:
         self.btnSelectAddr.setText(self.tr(\
            '%s Address: %s...' % (self.actStr, self.selectedAddr[:10])))


   #############################################################################
   def addrTableRxClicked(self, currIndex, prevIndex=None):
      if prevIndex == currIndex:
         return

      self.btnSelectAddr.setEnabled(True)
      row = currIndex.row()
      self.selectedAddr = currIndex.model().index(\
         row, ADDRESSCOLS.Address).data()
      self.selectedCmmt = currIndex.model().index(\
         row, ADDRESSCOLS.Comment).data()

      if not self.isBrowsingOnly:
         self.btnSelectAddr.setText(self.tr(\
            '%s Address: %s...' % (self.actStr, self.selectedAddr[:10])))

   #############################################################################
   def dblClickAddressRx(self, index):
      if index.column() != ADDRESSCOLS.Comment:
         self.acceptAddrSelection()
         return

      wlt = self.main.walletMap[self.selectedWltID]

      if not self.selectedCmmt:
         dialog = DlgSetComment(self, self.main, self.selectedCmmt, \
            self.tr('Add Address Comment'))
      else:
         dialog = DlgSetComment(self, self.main, self.selectedCmmt, \
            self.tr('Change Address Comment'))

      if dialog.exec_():
         newComment = str(dialog.edtComment.text())
         addr160 = addrStr_to_hash160(self.selectedAddr)[1]
         wlt.setComment(addr160, newComment)

   #############################################################################
   def getSelectedLBID(self):
      selection = self.lboxView.selectedIndexes()
      if len(selection)==0:
         return None
      row = selection[0].row()
      idCol = LOCKBOXCOLS.ID
      return str(self.lboxView.model().index(row, idCol).data().toString())

   #############################################################################
   def dblClickedLockbox(self, index):
      self.acceptLockBoxSelection()

   #############################################################################
   def clickedLockbox(self, currIndex, prevIndex=None):
      if prevIndex == currIndex:
         return

      row = currIndex.row()

      if not self.isBrowsingOnly:
         self.btnSelectAddr.setEnabled(True)
         self.useBareMultiSigCheckBox.setEnabled(True)
         selectedLockBoxId = str(currIndex.model().index(row, LOCKBOXCOLS.ID).data().toString())
         self.btnSelectAddr.setText( createLockboxEntryStr(selectedLockBoxId,
                                      self.useBareMultiSigCheckBox.isChecked()))

         # Disable Bare multisig if mainnet and N>3
         lb = self.main.getLockboxByID(selectedLockBoxId)
         if lb.N>3 and not USE_TESTNET and not USE_REGTEST:
            self.useBareMultiSigCheckBox.setEnabled(False)
            self.useBareMultiSigCheckBox.setChecked(False)
            self.useBareMultiSigCheckBox.setToolTip(self.tr(
               'Bare multi-sig is not available for M-of-N lockboxes on the '
               'main Bitcoin network with N higher than 3.'))
         else:
            self.useBareMultiSigCheckBox.setEnabled(True)

   #############################################################################
   def dblClickAddressTx(self, index):
      if index.column() != ADDRBOOKCOLS.Comment:
         self.acceptAddrSelection()
         return

      wlt = self.main.walletMap[self.selectedWltID]

      if not self.selectedCmmt:
         dialog = DlgSetComment(self, self.main, self.selectedCmmt, self.tr('Add Address Comment'))
      else:
         dialog = DlgSetComment(self, self.main, self.selectedCmmt, self.tr('Change Address Comment'))
      if dialog.exec_():
         newComment = str(dialog.edtComment.text())
         addr160 = addrStr_to_hash160(self.selectedAddr)[1]
         wlt.setComment(addr160, newComment)

   #############################################################################
   def acceptWltSelection(self):
      wltID = self.selectedWltID
      addrObj = self.main.walletMap[wltID].getNextUnusedAddress()
      self.target.setText(addrObj.getAddressString())
      self.target.setCursorPosition(0)
      self.accept()

   #############################################################################
   def useP2SHClicked(self):
      self.btnSelectAddr.setText( createLockboxEntryStr(self.getSelectedLBID(),
                                                    self.useBareMultiSigCheckBox.isChecked()))

   #############################################################################
   def acceptAddrSelection(self):
      if isBareLockbox(str(self.btnSelectAddr.text())) or \
         isP2SHLockbox(str(self.btnSelectAddr.text())):
         self.acceptLockBoxSelection()
      else:
         if self.target:
            self.target.setText(self.selectedAddr)
            self.target.setCursorPosition(0)
            self.accept()

   #############################################################################
   def acceptLockBoxSelection(self):
      if self.target:
         self.target.setText( createLockboxEntryStr(self.getSelectedLBID(),
                                                    self.useBareMultiSigCheckBox.isChecked()))
         self.target.setCursorPosition(0)
         self.accept()

   #############################################################################
   def getPubKeyForAddr160(self, addr160):
      if not self.returnPubKey:
         LOGERROR('Requested getPubKeyNotAddr, but looks like addr requested')

      wid = self.main.getWalletForAddrHash(addr160)
      if not wid:
         QtWidgets.QMessageBox.critical(self, self.tr('No Public Key'), self.tr(
            'This operation requires a full public key, not just an address. '
            'Unfortunately, Armory cannot find the public key for the address '
            'you selected.  In general public keys will only be available '
            'for addresses in your wallet.'), QtWidgets.QMessageBox.Ok)
         return None

      wlt = self.main.walletMap[wid]
      return wlt.getAddrByHash160(addr160).binPublicKey65.toHexStr()




   #############################################################################
   def showContextMenuTx(self, pos):
      menu = QtWidgets.QMenu(self.addrBookTxView)
      std = (self.main.usermode == USERMODE.Standard)
      adv = (self.main.usermode == USERMODE.Advanced)
      dev = (self.main.usermode == USERMODE.Expert)

      if True:  actionCopyAddr = menu.addAction(self.tr("Copy Address"))
      if dev:   actionCopyHash160 = menu.addAction(self.tr("Copy Hash160 (hex)"))
      if True:  actionCopyComment = menu.addAction(self.tr("Copy Comment"))
      idx = self.addrBookTxView.selectedIndexes()[0]
      action = menu.exec_(QtGui.QCursor.pos())

      if action == actionCopyAddr:
         s = self.addrBookTxView.model().index(idx.row(), ADDRBOOKCOLS.Address).data().toString()
      elif dev and action == actionCopyHash160:
         s = str(self.addrBookTxView.model().index(idx.row(), ADDRBOOKCOLS.Address).data().toString())
         atype, addr160 = addrStr_to_hash160(s)
         if atype==P2SHBYTE:
            LOGWARN('Copying Hash160 of P2SH address: %s' % s)
         s = binary_to_hex(addr160)
      elif action == actionCopyComment:
         s = self.addrBookTxView.model().index(idx.row(), ADDRBOOKCOLS.Comment).data().toString()
      else:
         return

      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(str(s).strip())


   #############################################################################
   def showContextMenuRx(self, pos):
      menu = QtWidgets.QMenu(self.addrBookRxView)
      std = (self.main.usermode == USERMODE.Standard)
      adv = (self.main.usermode == USERMODE.Advanced)
      dev = (self.main.usermode == USERMODE.Expert)

      if True:  actionCopyAddr = menu.addAction(self.tr("Copy Address"))
      if dev:   actionCopyHash160 = menu.addAction(self.tr("Copy Hash160 (hex)"))
      if True:  actionCopyComment = menu.addAction(self.tr("Copy Comment"))
      idx = self.addrBookRxView.selectedIndexes()[0]
      action = menu.exec_(QtGui.QCursor.pos())

      if action == actionCopyAddr:
         s = self.addrBookRxView.model().index(idx.row(), ADDRESSCOLS.Address).data().toString()
      elif dev and action == actionCopyHash160:
         s = str(self.addrBookRxView.model().index(idx.row(), ADDRESSCOLS.Address).data().toString())
         atype, addr160 = addrStr_to_hash160(s)
         if atype==P2SHBYTE:
            LOGWARN('Copying Hash160 of P2SH address: %s' % s)
         s = binary_to_hex(addr160)
      elif action == actionCopyComment:
         s = self.addrBookRxView.model().index(idx.row(), ADDRESSCOLS.Comment).data().toString()
      else:
         return

      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(str(s).strip())