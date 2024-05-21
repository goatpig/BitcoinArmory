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

from armoryengine.ArmoryUtils import USE_TESTNET, USE_REGTEST
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.qtdefines import AddToRunningDialogsList, GETFONT
from qtpy import QtCore, QtGui, QtWidgets

################################################################################
class ArmoryDialog(QtWidgets.QDialog):
   #create a signal with a random name that children to this dialog will
   #connect to close themselves if the parent is closed first

   closeSignal = QtCore.Signal()

   def __init__(self, parent=None, main=None):
      super(ArmoryDialog, self).__init__(parent)

      self.parent = parent
      self.main   = main

      #connect this dialog to the parent's close signal
      if self.parent is not None and hasattr(self.parent, 'closeSignal'):
         self.parent.closeSignal.connect(self.reject)

      self.setFont(GETFONT('var'))
      self.setWindowFlags(QtCore.Qt.Window)

      if USE_TESTNET:
         self.setWindowTitle(self.tr('Armory - Bitcoin Wallet Management [TESTNET] ' + self.__class__.__name__))
         self.setWindowIcon(QtGui.QIcon('./img/armory_icon_green_32x32.png'))
      elif USE_REGTEST:
         self.setWindowTitle(self.tr('Armory - Bitcoin Wallet Management [REGTEST] ' + self.__class__.__name__))
         self.setWindowIcon(QtGui.QIcon('./img/armory_icon_green_32x32.png'))
      else:
         self.setWindowTitle(self.tr('Armory - Bitcoin Wallet Management'))
         self.setWindowIcon(QtGui.QIcon('./img/armory_icon_32x32.png'))

   @AddToRunningDialogsList
   def exec_(self):
      return super(ArmoryDialog, self).exec_()

   def reject(self):
      self.closeSignal.emit()
      super(ArmoryDialog, self).reject()

   def executeMethod(self, _callable, *args):
      TheSignalExecution.executeMethod(_callable, *args)

   def callLater(self, delay, _callable, *args):
      TheSignalExecution.callLater(delay, _callable, *args)
