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

from qtpy import QtWidgets

from armoryengine.ArmoryUtils import LOGINFO
from qtdialogs.qtdefines import HLINE, QRichLabel
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgRestore import DlgRestoreSingle, DlgRestoreFragged, \
   DlgRestoreWOData

################################################################################
class DlgUniversalRestoreSelect(ArmoryDialog):

   #############################################################################
   def __init__(self, parent, main):
      super(DlgUniversalRestoreSelect, self).__init__(parent, main)


      lblDescrTitle = QRichLabel(self.tr('<b><u>Restore Wallet from Backup</u></b>'))
      lblDescr = QRichLabel(self.tr('You can restore any kind of backup ever created by Armory using '
         'one of the options below.  If you have a list of private keys '
         'you should open the target wallet and select "Import/Sweep '
         'Private Keys."'))

      self.rdoSingle = QtWidgets.QRadioButton(self.tr('Single-Sheet Backup (printed)'))
      self.rdoFragged = QtWidgets.QRadioButton(self.tr('Fragmented Backup (incl. mix of paper and files)'))
      self.rdoDigital = QtWidgets.QRadioButton(self.tr('Import digital backup or watching-only wallet'))
      self.rdoWOData = QtWidgets.QRadioButton(self.tr('Import watching-only wallet data'))
      self.chkTest = QtWidgets.QCheckBox(self.tr('This is a test recovery to make sure my backup works'))
      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.rdoSingle)
      btngrp.addButton(self.rdoFragged)
      btngrp.addButton(self.rdoDigital)
      btngrp.addButton(self.rdoWOData)
      btngrp.setExclusive(True)

      self.rdoSingle.setChecked(True)
      self.rdoSingle.clicked.connect(self.clickedRadio)
      self.rdoFragged.clicked.connect(self.clickedRadio)
      self.rdoDigital.clicked.connect(self.clickedRadio)
      self.rdoWOData.clicked.connect(self.clickedRadio)

      self.btnOkay = QtWidgets.QPushButton(self.tr('Continue'))
      self.btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnOkay, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      self.btnOkay.clicked.connect(self.clickedOkay)
      self.btnCancel.clicked.connect(self.reject)


      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescrTitle)
      layout.addWidget(lblDescr)
      layout.addWidget(HLINE())
      layout.addWidget(self.rdoSingle)
      layout.addWidget(self.rdoFragged)
      layout.addWidget(self.rdoDigital)
      layout.addWidget(self.rdoWOData)
      layout.addWidget(HLINE())
      layout.addWidget(self.chkTest)
      layout.addWidget(buttonBox)
      self.setLayout(layout)
      self.setMinimumWidth(450)

   def clickedRadio(self):
      if self.rdoDigital.isChecked():
         self.chkTest.setChecked(False)
         self.chkTest.setEnabled(False)
      else:
         self.chkTest.setEnabled(True)

   def clickedOkay(self):
        # ## Test backup option

      doTest = self.chkTest.isChecked()

      if self.rdoSingle.isChecked():
         self.accept()
         dlg = DlgRestoreSingle(self.parent, self.main, doTest)
         if dlg.exec_():
            self.main.addWalletToApplication(dlg.newWallet)
            LOGINFO('Wallet Restore Complete!')

      elif self.rdoFragged.isChecked():
         self.accept()
         dlg = DlgRestoreFragged(self.parent, self.main, doTest)
         if dlg.exec_():
            self.main.addWalletToApplication(dlg.newWallet)
            LOGINFO('Wallet Restore Complete!')
      elif self.rdoDigital.isChecked():
         self.main.execGetImportWltName()
         self.accept()
      elif self.rdoWOData.isChecked():
            # Attempt to restore the root public key & chain code for a wallet.
            # When done, ask for a wallet rescan.
         self.accept()
         dlg = DlgRestoreWOData(self.parent, self.main, doTest)
         if dlg.exec_():
            LOGINFO('Watching-Only Wallet Restore Complete! Will ask for a' \
                    'rescan.')
            self.main.addWalletToApplication(dlg.newWallet)