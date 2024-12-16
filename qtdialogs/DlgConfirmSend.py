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

from armoryengine.ArmoryUtils import CPP_TXOUT_HAS_ADDRSTR, \
   CPP_TXOUT_P2WPKH, CPP_TXOUT_P2WSH, coin2strNZS, coin2str
from armoryengine.Transaction import getTxOutScriptType
from armoryengine.AddressUtils import script_to_scrAddr, scrAddr_to_hash160
from armorycolors import htmlColor

from qtdialogs.qtdefines import USERMODE, QRichLabel, \
   GETFONT, HLINE, makeLayoutFrame, makeVertFrame, makeHorizFrame, \
   VERTICAL, STYLE_RAISED
from qtdialogs.DlgDispTxInfo import DlgDispTxInfo
from qtdialogs.ArmoryDialog import ArmoryDialog

#############################################################################
def excludeChange(outputPairs, wlt):
   """
   NOTE:  this method works ONLY because we always generate a new address
          whenever creating a change-output, which means it must have a
          higher chainIndex than all other addresses.  If you did something
          creative with this tx, this may not actually work.
   """
   maxChainIndex = -5
   nonChangeOutputPairs = []
   currentMaxChainPair = None
   for script,val in outputPairs:
      scrType = getTxOutScriptType(script)
      addr = ''
      if scrType in CPP_TXOUT_HAS_ADDRSTR:
         scrAddr = script_to_scrAddr(script)
         addr = wlt.getAddrByHash(scrAddr)

      # this logic excludes the pair with the maximum chainIndex from the
      # returned list
      if addr:
         if addr.chainIndex > maxChainIndex:
            maxChainIndex = addr.chainIndex
            if currentMaxChainPair:
               nonChangeOutputPairs.append(currentMaxChainPair)
            currentMaxChainPair = [script,val]
         else:
            nonChangeOutputPairs.append([script,val])
   return nonChangeOutputPairs

################################################################################
class DlgConfirmSend(ArmoryDialog):

   def __init__(self, wlt, scriptValPairs, fee, parent=None, main=None, \
                                          sendNow=False, pytxOrUstx=None):
      super(DlgConfirmSend, self).__init__(parent, main)
      layout = QtWidgets.QGridLayout()
      lblInfoImg = QtWidgets.QLabel()
      lblInfoImg.setPixmap(QtGui.QPixmap(':/MsgBox_info48.png'))
      lblInfoImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)

      changeRemoved = False
      sendPairs = []
      returnPairs = []
      for script,val in scriptValPairs:
         scrType = getTxOutScriptType(script)
         scraddr = script_to_scrAddr(script)
         if wlt.hasAddrHash(scraddr):
            returnPairs.append([script,val])
         else:
            sendPairs.append([script,val])

      # If there are more than 3 return pairs then this is a 1%'er tx we should
      # not presume to know which pair is change. It's a weird corner case so
      # it's best to leave it alone.
      # 0 return is an exact change tx, no need to deal with it so this
      # chunk of code that removes change only cares about 1 and 2 return pairs.
      # if 1 remove it, if 2 remove the one with a higher index.
      # Exception: IF no send pairs, it's a tx for max to a single internal address

      if len(sendPairs)==1 and len(returnPairs)==0:
         # Exactly one output, exact change by definition
         doExcludeChange = False
         doShowAllMsg    = False
         doShowLeaveWlt  = False
      elif len(sendPairs)==0 and len(returnPairs)==1:
         doExcludeChange = False
         doShowAllMsg    = False
         doShowLeaveWlt  = True
      elif len(returnPairs)==0:
         # Exact change
         doExcludeChange = False
         doShowAllMsg    = False
         doShowLeaveWlt  = False
      elif len(returnPairs)==1:
         # There's a simple change output
         doExcludeChange = True
         doShowAllMsg    = False
         doShowLeaveWlt  = False
      elif len(sendPairs)==0 and len(returnPairs)==2:
         # Send-to-self within wallet, with change, no external recips
         doExcludeChange = True
         doShowAllMsg    = False
         doShowLeaveWlt  = True
      else:
         # Everything else just show everything
         doExcludeChange = False
         doShowAllMsg    = True
         doShowLeaveWlt  = True


      if doExcludeChange:
         returnPairs = excludeChange(returnPairs, wlt)


      # returnPairs now includes only the outputs to be displayed
      totalLeavingWlt = sum([val for script,val in sendPairs]) + fee
      totalSend       = sum([val for script,val in returnPairs]) + totalLeavingWlt
      sendFromWalletStr = coin2strNZS(totalLeavingWlt)
      totalSendStr      = coin2strNZS(totalSend)


      lblAfterBox = QRichLabel('')

      # Always include a way to review the tx when in expert mode.  Or whenever
      # we are showing the entire output list.
      # If we have a pytx or ustx, we can add a DlgDispTxInfo button
      showAllMsg = ''
      if doShowAllMsg or (pytxOrUstx and self.main.usermode==USERMODE.Expert):
         showAllMsg = self.tr('To see complete transaction details '
                             '<a href="None">click here</a></font>')

         def openDlgTxInfo(*args):
            DlgDispTxInfo(pytxOrUstx, wlt, self.parent, self.main).exec_()

         lblAfterBox.linkActivated.connect(openDlgTxInfo)


      lblMsg = QRichLabel(self.tr(
         'This transaction will spend <b>%s BTC</b> from '
         '<font color="%s">Wallet "<b>%s</b>" (%s)</font> to the following '
         'recipients:' % (totalSendStr, htmlColor('TextBlue'), wlt.labelName, wlt.uniqueIDB58)))

      if doShowLeaveWlt:
         lblAfterBox.setText(self.tr(
            '<font size=3>* Starred '
            'outputs are going to the same wallet from which they came '
            'and do not affect the wallet\'s final balance. '
            'The total balance of the wallet will actually only decrease '
            '<b>%s BTC</b> as a result of this transaction.  %s</font>' % (sendFromWalletStr, showAllMsg)))
      elif len(showAllMsg)>0:
         lblAfterBox.setText(showAllMsg)


      addrColWidth = 50

      recipLbls = []
      ffixBold = GETFONT('Fixed')
      ffixBold.setWeight(QtGui.QFont.Bold)
      for script,val in sendPairs + returnPairs:
         displayInfo = self.main.getDisplayStringForScript(script, addrColWidth)
         dispStr = (' '+displayInfo['String']).ljust(addrColWidth)

         coinStr = coin2str(val, rJust=True, maxZeros=4)
         if [script,val] in returnPairs:
            dispStr = '*'+dispStr[1:]

         recipLbls.append(QtWidgets.QLabel(dispStr + coinStr))
         recipLbls[-1].setFont(ffixBold)


      if fee > 0:
         recipLbls.append(QtWidgets.QSpacerItem(10, 10))
         recipLbls.append(QtWidgets.QLabel(' Transaction Fee : '.ljust(addrColWidth) +
                           coin2str(fee, rJust=True, maxZeros=4)))
         recipLbls[-1].setFont(GETFONT('Fixed'))


      recipLbls.append(HLINE(QtWidgets.QFrame.Sunken))
      if doShowLeaveWlt:
         # We have a separate message saying "total amount actually leaving wlt is..."
         # We can just give the total of all the outputs in the table above
         recipLbls.append(QtWidgets.QLabel(' Total: '.ljust(addrColWidth) +
                           coin2str(totalSend, rJust=True, maxZeros=4)))
      else:
         # The k
         recipLbls.append(QtWidgets.QLabel(' Total Leaving Wallet: '.ljust(addrColWidth) +
                           coin2str(totalSend, rJust=True, maxZeros=4)))

      recipLbls[-1].setFont(GETFONT('Fixed'))

      if sendNow:
         self.btnAccept = QtWidgets.QPushButton(self.tr('Send'))
         lblLastConfirm = QtWidgets.QLabel(self.tr('Are you sure you want to execute this transaction?'))
      else:
         self.btnAccept = QtWidgets.QPushButton(self.tr('Continue'))
         lblLastConfirm = QtWidgets.QLabel(self.tr('Does the above look correct?'))

      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept.clicked.connect(self.accept)
      self.btnCancel.clicked.connect(self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      isSigned = pytxOrUstx.verifySigsAllInputs()
      frmBtnSelect = buttonBox

      frmTable = makeLayoutFrame(VERTICAL, recipLbls, STYLE_RAISED)
      frmRight = makeVertFrame([ lblMsg, \
                                  'Space(20)', \
                                  frmTable, \
                                  lblAfterBox, \
                                  'Space(10)', \
                                  lblLastConfirm, \
                                  'Space(10)', \
                                  frmBtnSelect ])

      frmAll = makeHorizFrame([ lblInfoImg, frmRight ])

      layout.addWidget(frmAll)

      self.setLayout(layout)
      self.setMinimumWidth(350)
      self.setWindowTitle(self.tr('Confirm Transaction'))