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

from armoryengine.CppBridge import BridgePrompt
from armoryengine.BDM import TheBDM

from qtpy import QtWidgets
from qtdialogs.qtdefines import relaxedSizeStr, STRETCH, \
   STYLE_SUNKEN, QRichLabel, makeHorizFrame, makeVertFrame
from armorycolors import htmlColor

from qtdialogs.ArmoryDialog import ArmoryDialog
from ui.QtExecuteSignal import TheSignalExecution

################################################################################
class DlgKeypoolSettings(ArmoryDialog, BridgePrompt):
   """
   Let the user manually adjust the keypool for this wallet
   """
   def __init__(self, wlt, parent=None, main=None):
      BridgePrompt.__init__(self)
      ArmoryDialog.__init__(self, parent, main)

      self.wlt = wlt
      self.addressesWereGenerated = False
      self.lblDescr = QRichLabel(self.tr(
         'Armory pre-computes a pool of addresses beyond the last address '
         'you have used, and keeps them in your wallet to "look-ahead."  One '
         'reason it does this is in case you have restored this wallet from '
         'a backup, and Armory does not know how many addresses you have actually '
         'used. '
         '<br><br>'
         'If this wallet was restored from a backup and was very active after '
         'it was backed up, then it is possible Armory did not pre-compute '
         'enough addresses to find your entire balance.  <b>This condition is '
         'rare</b>, but it can happen.  You may extend the keypool manually, '
         'below.'))

      self.lblAddrUsed = QRichLabel(self.tr('Addresses used: '), doWrap=False)
      self.lblAddrComp = QRichLabel(self.tr('Addresses computed: '), doWrap=False)
      self.lblAddrUsedVal = QRichLabel('%d' % max(0, self.wlt.getHighestUsedIndex()))
      self.lblAddrCompVal = QRichLabel('%d' % self.wlt.lastComputedChainIndex)

      self.lblNumAddr = QRichLabel(self.tr('Compute this many more addresses: '))
      self.edtNumAddr = QtWidgets.QLineEdit()
      self.edtNumAddr.setText('100')
      self.edtNumAddr.setMaximumWidth(relaxedSizeStr(self, '9999999')[0])

      self.lblWarnSpeed = QRichLabel(self.tr(
         'Address computation is very slow.  It may take up to one minute '
         'to compute 200-1000 addresses (system-dependent).  Only generate '
         'as many as you think you need.'))

      buttonBox = QtWidgets.QDialogButtonBox()
      self.btnAccept = QtWidgets.QPushButton(self.tr("Compute"))
      self.btnReject = QtWidgets.QPushButton(self.tr("Done"))
      self.btnAccept.clicked.connect(self.clickCompute)
      self.btnReject.clicked.connect(self.reject)
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnReject, QtWidgets.QDialogButtonBox.RejectRole)

      frmLbl = makeVertFrame([self.lblAddrUsed, self.lblAddrComp])
      frmVal = makeVertFrame([self.lblAddrUsedVal, self.lblAddrCompVal])

      subFrm1 = makeHorizFrame([
            STRETCH,
            frmLbl,
            frmVal,
            STRETCH
         ], STYLE_SUNKEN)
      subFrm2 = makeHorizFrame([
            STRETCH,
            self.lblNumAddr,
            self.edtNumAddr,
            STRETCH
         ], STYLE_SUNKEN)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(self.lblDescr)
      layout.addWidget(subFrm1)
      layout.addWidget(self.lblWarnSpeed)
      layout.addWidget(subFrm2)
      layout.addWidget(buttonBox)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Extend Address Pool'))

   #############################################################################
   def parseProtoPacket(self, payload):
      if payload.which() == 'cleanup':
         TheBDM.unregisterPrompt(self.promptId)
         return

      elif payload.which() == 'walletProgress':
         wltProg = payload.walletProgress
         if wltProg.which() == 'extendChain':
            extChain = wltProg.extendChain
            TheSignalExecution.executeMethod(self.updateProgress,
               extChain.current, extChain.total)

   ####
   def updateProgress(self, current, total):
      cred = htmlColor('TextRed')
      self.lblAddrCompVal.setText(self.tr(
         f'<font color=\"{cred}\">Calculating: {current}/{total}</font>'))

   ####
   def clickCompute(self):
      err = False
      try:
         naddr = int(self.edtNumAddr.text())
      except:
         err = True

      if err or naddr < 1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid input'), self.tr(
            'The value you entered is invalid.  Please enter a positive '
            'number of addresses to generate.'), QtWidgets.QMessageBox.Ok)
         return

      if naddr >= 1000:
         confirm = QtWidgets.QMessageBox.warning(self,
            self.tr('Are you sure?'), self.tr(
               'You have entered that you want to compute %s more addresses'
               'for this wallet.  This operation will take a very long time, '
               'and Armory will become unresponsive until the computation is '
               'finished.  Armory estimates it will take about %d minutes.'
               '<br><br>Do you want to continue?' % (naddr, int(naddr / 750.))
            ), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
         if confirm != QtWidgets.QMessageBox.Yes:
            return

      cred = htmlColor('TextRed')
      self.lblAddrCompVal.setText(self.tr(
         '<font color="%s">Calculating...</font>' % cred))

      def completedCallback():
         TheSignalExecution.executeMethod(self.completeCompute)
      self.wlt.fillAddressPool(naddr, self.promptId,
         lambda a: TheSignalExecution.executeMethod(self.completeCompute, a))

   ####
   def completeCompute(self, packet):
      if packet.success == False:
         QtWidgets.QMessageBox.error(self, self.tr('Error'),
         self.tr('Failed to extend addresses!'),
         QtWidgets.QMessageBox.Ok)
      else:
         naddr = int(self.edtNumAddr.text())
         QtWidgets.QMessageBox.information(self, self.tr('Success'),
         self.tr(f'{naddr} addresses have been added to the wallet'),
         QtWidgets.QMessageBox.Ok)
         self.addressesWereGenerated = True
         self.accept()
