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

from qtpy import QtCore, QtWidgets


from armoryengine.Settings import TheSettings

from qtdialogs.DlgOfflineTx import DlgOfflineTxCreated
from qtdialogs.ArmoryDialog import ArmoryDialog

from ui.TxFrames import SendBitcoinsFrame

################################################################################
class DlgSendBitcoins(ArmoryDialog):
   def __init__(self, wlt, parent=None, main=None,
                              wltIDList=None, onlyOfflineWallets=False,
                              spendFromLockboxID=None):
      super(DlgSendBitcoins, self).__init__(parent, main)
      layout = QtWidgets.QVBoxLayout()

      self.spendFromLockboxID = spendFromLockboxID

      self.frame = SendBitcoinsFrame(self, main, self.tr('Send Bitcoins'),
                   wlt, wltIDList, onlyOfflineWallets=onlyOfflineWallets,
                   sendCallback=self.createTxAndBroadcast,
                   createUnsignedTxCallback=self.createUnsignedTxAndDisplay,
                   spendFromLockboxID=spendFromLockboxID)
      layout.addWidget(self.frame)
      self.setLayout(layout)
      self.sizeHint = lambda: QtCore.QSize(850, 600)
      self.setMinimumWidth(700)
      # Update the any controls based on the initial wallet selection
      self.frame.fireWalletChange()



   #############################################################################
   def createUnsignedTxAndDisplay(self, ustx):
      self.accept()
      if self.spendFromLockboxID is None:
         dlg = DlgOfflineTxCreated(self.frame.wlt, ustx, self.parent, self.main)
         dlg.exec_()
      else:
         dlg = DlgMultiSpendReview(self.parent, self.main, ustx)
         dlg.exec_()


   #############################################################################
   def createTxAndBroadcast(self):
      self.accept()

   #############################################################################
   def saveGeometrySettings(self):
      geom = self.saveGeometry().data().hex()
      TheSettings.set('SendBtcGeometry', geom)

   #############################################################################
   def closeEvent(self, event):
      self.saveGeometrySettings()
      super(DlgSendBitcoins, self).closeEvent(event)

   #############################################################################
   def accept(self, *args):
      self.saveGeometrySettings()
      super(DlgSendBitcoins, self).accept(*args)

   #############################################################################
   def reject(self, *args):
      self.saveGeometrySettings()
      super(DlgSendBitcoins, self).reject(*args)