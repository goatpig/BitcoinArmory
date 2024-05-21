##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2022, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from qtdialogs.ArmoryDialog import ArmoryDialog
from armoryengine.AddressUtils import encodePrivKeyBase58

################################################################################
class DlgShowKeyList(ArmoryDialog):
   def __init__(self, wlt, parent=None, main=None):
      super(DlgShowKeyList, self).__init__(parent, main)

      self.wlt = wlt

      self.havePriv = ((not self.wlt.useEncryption) or not (self.wlt.isLocked))

      wltType = determineWalletType(self.wlt, self.main)[0]
      if wltType in (WLTTYPES.Offline, WLTTYPES.WatchOnly):
         self.havePriv = False


      # NOTE/WARNING:  We have to make copies (in RAM) of the unencrypted
      #                keys, or else we will have to type in our address
      #                every 10s if we want to modify the key list.  This
      #                isn't likely a big problem, but it's not ideal,
      #                either.  Not much I can do about, though...
      #                (at least:  once this dialog is closed, all the
      #                garbage should be collected...)
      self.addrCopies = []
      for addr in self.wlt.getLinearAddrList(withAddrPool=True):
         self.addrCopies.append(addr.copy())
      self.rootKeyCopy = self.wlt.addrMap['ROOT'].copy()

      backupVersion = BACKUP_TYPE_135A
      testChain = DeriveChaincodeFromRootKey(self.rootKeyCopy.binPrivKey32_Plain)
      self.needChaincode = (not testChain == self.rootKeyCopy.chaincode)
      if not self.needChaincode:
         backupVersion = BACKUP_TYPE_135C

      self.strDescrReg = (self.tr(
         'The textbox below shows all keys that are part of this wallet, '
         'which includes both permanent keys and imported keys.  If you '
         'simply want to backup your wallet and you have no imported keys '
         'then all data below is reproducible from a plain paper backup. '
         '<br><br> '
         'If you have imported addresses to backup, and/or you '
         'would like to export your private keys to another '
         'wallet service or application, then you can save this data '
         'to disk, or copy&paste it into the other application.'))
      self.strDescrWarn = (self.tr(
         '<br><br>'
         '<font color="red">Warning:</font> The text box below contains '
         'the plaintext (unencrypted) private keys for each of '
         'the addresses in this wallet.  This information can be used '
         'to spend the money associated with those addresses, so please '
         'protect it like you protect the rest of your wallet. '))

      self.lblDescr = QRichLabel('')
      self.lblDescr.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignTop)


      txtFont = GETFONT('Fixed', 8)
      self.txtBox = QtWidgets.QTextEdit()
      self.txtBox.setReadOnly(True)
      self.txtBox.setFont(txtFont)
      w, h = tightSizeNChar(txtFont, 110)
      self.txtBox.setFont(txtFont)
      self.txtBox.setMinimumWidth(w)
      self.txtBox.setMaximumWidth(w)
      self.txtBox.setMinimumHeight(h * 3.2)
      self.txtBox.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Preferred)

      # Create a list of checkboxes and then some ID word to identify what
      # to put there
      self.chkList = {}
      self.chkList['AddrStr'] = QtWidgets.QCheckBox(self.tr('Address String'))
      self.chkList['PubKeyHash'] = QtWidgets.QCheckBox(self.tr('Hash160'))
      self.chkList['PrivCrypt'] = QtWidgets.QCheckBox(self.tr('Private Key (Encrypted)'))
      self.chkList['PrivHexBE'] = QtWidgets.QCheckBox(self.tr('Private Key (Plain Hex)'))
      self.chkList['PrivB58'] = QtWidgets.QCheckBox(self.tr('Private Key (Plain Base58)'))
      self.chkList['PubKey'] = QtWidgets.QCheckBox(self.tr('Public Key (BE)'))
      self.chkList['ChainIndex'] = QtWidgets.QCheckBox(self.tr('Chain Index'))

      self.chkList['AddrStr'   ].setChecked(True)
      self.chkList['PubKeyHash'].setChecked(False)
      self.chkList['PrivB58'   ].setChecked(self.havePriv)
      self.chkList['PrivCrypt' ].setChecked(False)
      self.chkList['PrivHexBE' ].setChecked(self.havePriv)
      self.chkList['PubKey'    ].setChecked(not self.havePriv)
      self.chkList['ChainIndex'].setChecked(False)

      namelist = ['AddrStr', 'PubKeyHash', 'PrivB58', 'PrivCrypt', \
                  'PrivHexBE', 'PubKey', 'ChainIndex']

      for name in self.chkList.keys():
         self.connect(self.chkList[name], SIGNAL('toggled(bool)'), \
                      self.rewriteList)


      self.chkImportedOnly = QtWidgets.QCheckBox(self.tr('Imported Addresses Only'))
      self.chkWithAddrPool = QtWidgets.QCheckBox(self.tr('Include Unused (Address Pool)'))
      self.chkDispRootKey = QtWidgets.QCheckBox(self.tr('Include Paper Backup Root'))
      self.chkOmitSpaces = QtWidgets.QCheckBox(self.tr('Omit spaces in key data'))
      self.chkDispRootKey.setChecked(True)
      self.connect(self.chkImportedOnly, SIGNAL('toggled(bool)'), self.rewriteList)
      self.connect(self.chkWithAddrPool, SIGNAL('toggled(bool)'), self.rewriteList)
      self.connect(self.chkDispRootKey, SIGNAL('toggled(bool)'), self.rewriteList)
      self.connect(self.chkOmitSpaces, SIGNAL('toggled(bool)'), self.rewriteList)
      # self.chkCSV = QtWidgets.QCheckBox('Display in CSV format')

      if not self.havePriv:
         self.chkDispRootKey.setChecked(False)
         self.chkDispRootKey.setEnabled(False)


      std = (self.main.usermode == USERMODE.Standard)
      adv = (self.main.usermode == USERMODE.Advanced)
      dev = (self.main.usermode == USERMODE.Expert)
      if std:
         self.chkList['PubKeyHash'].setVisible(False)
         self.chkList['PrivCrypt' ].setVisible(False)
         self.chkList['ChainIndex'].setVisible(False)
      elif adv:
         self.chkList['PubKeyHash'].setVisible(False)
         self.chkList['ChainIndex'].setVisible(False)

      # We actually just want to remove these entirely
      # (either we need to display all data needed for decryption,
      # besides passphrase,  or we shouldn't show any of it)
      self.chkList['PrivCrypt' ].setVisible(False)

      chkBoxList = [self.chkList[n] for n in namelist]
      chkBoxList.append('Line')
      chkBoxList.append(self.chkImportedOnly)
      chkBoxList.append(self.chkWithAddrPool)
      chkBoxList.append(self.chkDispRootKey)

      frmChks = makeLayoutFrame(VERTICAL, chkBoxList, STYLE_SUNKEN)


      btnGoBack = QtWidgets.QPushButton(self.tr('<<< Go Back'))
      btnSaveFile = QtWidgets.QPushButton(self.tr('Save to File...'))
      btnCopyClip = QtWidgets.QPushButton(self.tr('Copy to Clipboard'))
      self.lblCopied = QRichLabel('')

      self.connect(btnGoBack, SIGNAL(CLICKED), self.accept)
      self.connect(btnSaveFile, SIGNAL(CLICKED), self.saveToFile)
      self.connect(btnCopyClip, SIGNAL(CLICKED), self.copyToClipboard)
      frmGoBack = makeLayoutFrame(HORIZONTAL, [btnGoBack, \
                                            STRETCH, \
                                            self.chkOmitSpaces, \
                                            STRETCH, \
                                            self.lblCopied, \
                                            btnCopyClip, \
                                            btnSaveFile])

      frmDescr = makeLayoutFrame(HORIZONTAL, [self.lblDescr], STYLE_SUNKEN)

      if not self.havePriv or (self.wlt.useEncryption and self.wlt.isLocked):
         self.chkList['PrivHexBE'].setEnabled(False)
         self.chkList['PrivHexBE'].setChecked(False)
         self.chkList['PrivB58'  ].setEnabled(False)
         self.chkList['PrivB58'  ].setChecked(False)

      dlgLayout = QtWidgets.QGridLayout()
      dlgLayout.addWidget(frmDescr, 0, 0, 1, 1)
      dlgLayout.addWidget(frmChks, 0, 1, 1, 1)
      dlgLayout.addWidget(self.txtBox, 1, 0, 1, 2)
      dlgLayout.addWidget(frmGoBack, 2, 0, 1, 2)
      dlgLayout.setRowStretch(0, 0)
      dlgLayout.setRowStretch(1, 1)
      dlgLayout.setRowStretch(2, 0)

      self.setLayout(dlgLayout)
      self.rewriteList()
      self.setWindowTitle(self.tr('All Wallet Keys'))

   def rewriteList(self, *args):
      """
      Write out all the wallet data
      """
      whitespace = '' if self.chkOmitSpaces.isChecked() else ' '

      def fmtBin(s, nB=4, sw=False):
         h = binary_to_hex(s)
         if sw:
            h = hex_switchEndian(h)
         return whitespace.join([h[i:i + nB] for i in range(0, len(h), nB)])

      L = []
      L.append('Created:       ' + unixTimeToFormatStr(RightNow(), self.main.getPreferredDateFormat()))
      L.append('Wallet ID:     ' + self.wlt.uniqueIDB58)
      L.append('Wallet Name:   ' + self.wlt.labelName)
      L.append('')

      if self.chkDispRootKey.isChecked():
         binPriv0 = self.rootKeyCopy.binPrivKey32_Plain.toBinStr()[:16]
         binPriv1 = self.rootKeyCopy.binPrivKey32_Plain.toBinStr()[16:]
         binChain0 = self.rootKeyCopy.chaincode.toBinStr()[:16]
         binChain1 = self.rootKeyCopy.chaincode.toBinStr()[16:]
         binPriv0Chk = computeChecksum(binPriv0, nBytes=2)
         binPriv1Chk = computeChecksum(binPriv1, nBytes=2)
         binChain0Chk = computeChecksum(binChain0, nBytes=2)
         binChain1Chk = computeChecksum(binChain1, nBytes=2)

         binPriv0 = binary_to_easyType16(binPriv0 + binPriv0Chk)
         binPriv1 = binary_to_easyType16(binPriv1 + binPriv1Chk)
         binChain0 = binary_to_easyType16(binChain0 + binChain0Chk)
         binChain1 = binary_to_easyType16(binChain1 + binChain1Chk)

         L.append('-' * 80)
         L.append('The following is the same information contained on your paper backup.')
         L.append('All NON-imported addresses in your wallet are backed up by this data.')
         L.append('')
         L.append('Root Key:     ' + ' '.join([binPriv0[i:i + 4]  for i in range(0, 36, 4)]))
         L.append('              ' + ' '.join([binPriv1[i:i + 4]  for i in range(0, 36, 4)]))
         if self.needChaincode:
            L.append('Chain Code:   ' + ' '.join([binChain0[i:i + 4] for i in range(0, 36, 4)]))
            L.append('              ' + ' '.join([binChain1[i:i + 4] for i in range(0, 36, 4)]))
         L.append('-' * 80)
         L.append('')

         # Cleanup all that sensitive data laying around in RAM
         binPriv0, binPriv1 = None, None
         binChain0, binChain1 = None, None
         binPriv0Chk, binPriv1Chk = None, None
         binChain0Chk, binChain1Chk = None, None

      self.havePriv = False
      topChain = self.wlt.highestUsedChainIndex
      extraLbl = ''

      for addr in self.addrCopies:
         try:
            cppAddrObj = self.wlt.cppWallet.getAddrObjByIndex(addr.chainIndex)
         except:
            addrIndex = self.wlt.cppWallet.getAssetIndexForAddr(addr.getAddr160())
            cppAddrObj = self.wlt.cppWallet.getAddrObjByIndex(addrIndex)

         # Address pool
         if self.chkWithAddrPool.isChecked():
            if addr.chainIndex > topChain:
               extraLbl = '   (Unused/Address Pool)'
         else:
            if addr.chainIndex > topChain:
               continue

         # Imported Addresses
         if self.chkImportedOnly.isChecked():
            if not addr.chainIndex == -2:
               continue
         else:
            if addr.chainIndex == -2:
               extraLbl = '   (Imported)'

         if self.chkList['AddrStr'   ].isChecked():
            L.append(cppAddrObj.getScrAddr() + extraLbl)
         if self.chkList['PubKeyHash'].isChecked():
            L.append('   Hash160   : ' + fmtBin(addr.getAddr160()))
         if self.chkList['PrivB58'   ].isChecked():
            pB58 = encodePrivKeyBase58(addr.binPrivKey32_Plain.toBinStr())
            pB58Stretch = whitespace.join([pB58[i:i + 6] for i in range(0, len(pB58), 6)])
            L.append('   PrivBase58: ' + pB58Stretch)
            self.havePriv = True
         if self.chkList['PrivCrypt' ].isChecked():
            L.append('   PrivCrypt : ' + fmtBin(addr.binPrivKey32_Encr.toBinStr()))
         if self.chkList['PrivHexBE' ].isChecked():
            L.append('   PrivHexBE : ' + fmtBin(addr.binPrivKey32_Plain.toBinStr()))
            self.havePriv = True
         if self.chkList['PubKey'    ].isChecked():
            L.append('   PublicX   : ' + fmtBin(addr.binPublicKey65.toBinStr()[1:33 ]))
            L.append('   PublicY   : ' + fmtBin(addr.binPublicKey65.toBinStr()[  33:]))
         if self.chkList['ChainIndex'].isChecked():
            L.append('   ChainIndex: ' + str(addr.chainIndex))

      self.txtBox.setText('\n'.join(L))
      if self.havePriv:
         self.lblDescr.setText(self.strDescrReg + self.strDescrWarn)
      else:
         self.lblDescr.setText(self.strDescrReg)

   def saveToFile(self):
      if self.havePriv:
         if not TheSettings.getSettingOrSetDefault('DNAA_WarnPrintKeys', False):
            result = MsgBoxWithDNAA(self, self.main, MSGBOX.Warning, title=self.tr('Plaintext Private Keys'), \
                  msg=self.tr('<font color="red"><b>REMEMBER:</b></font> The data you '
                  'are about to save contains private keys.  Please make sure '
                  'that only trusted persons will have access to this file. '
                  '<br><br>Are you sure you want to continue?'), \
                  dnaaMsg=None, wCancel=True)
            if not result[0]:
               return
            TheSettings.set('DNAA_WarnPrintKeys', result[1])

      wltID = self.wlt.uniqueIDB58
      fn = self.main.getFileSave(title=self.tr('Save Key List'), \
                                 ffilter=[self.tr('Text Files (*.txt)')], \
                                 defaultFilename=('keylist_%s_.txt' % wltID))
      if len(fn) > 0:
         fileobj = open(fn, 'w')
         fileobj.write(str(self.txtBox.toPlainText()))
         fileobj.close()



   def copyToClipboard(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(str(self.txtBox.toPlainText()))
      self.lblCopied.setText(self.tr('<i>Copied!</i>'))


   def cleanup(self):
      self.rootKeyCopy.binPrivKey32_Plain.destroy()
      for addr in self.addrCopies:
         addr.binPrivKey32_Plain.destroy()
      self.rootKeyCopy = None
      self.addrCopies = None

   def accept(self):
      self.cleanup()
      super(DlgShowKeyList, self).accept()

   def reject(self):
      self.cleanup()
      super(DlgShowKeyList, self).reject()