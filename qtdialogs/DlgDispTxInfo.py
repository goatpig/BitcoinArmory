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

from armoryengine.ArmoryUtils import enum, \
   CPP_TXOUT_HAS_ADDRSTR, BIGENDIAN, binary_to_hex, \
   hex_to_binary, coin2str, coin2strNZS, LOGEXCEPT, LOGERROR, \
   CPP_TXIN_SCRIPT_NAMES, CPP_TXOUT_SCRIPT_NAMES, int_to_hex, \
   unixTimeToFormatStr, UINT32_MAX, hash256
from armoryengine.AddressUtils import addrStr_to_hash160, \
   script_to_scrAddr, script_to_addrStr, scrAddr_to_addrStr, \
   addrStr_to_scrAddr, script_to_scrAddr

from armoryengine.BDM import TheBDM, BDM_BLOCKCHAIN_READY
from armoryengine.Transaction import UnsignedTransaction, \
   determineSentToSelfAmt, PyTx, getTxInScriptType, getTxOutScriptType, \
   TxInExtractAddrStrIfAvail
from armoryengine.Block import PyBlockHeader
from armoryengine.Script import convertScriptToOpStrings
from armoryengine.CppBridge import TheBridge
from armoryengine.Settings import TheSettings

from armorymodels import TxInDispModel, TxOutDispModel, TXINCOLS, TXOUTCOLS
from qtdialogs.qtdefines import STYLE_RAISED, USERMODE, VERTICAL, \
   QRichLabel, relaxedSizeStr, GETFONT, tightSizeNChar, STYLE_SUNKEN, \
   HORIZONTAL, makeHorizFrame, initialColResize, makeLayoutFrame, STRETCH, \
   createToolTipWidget
from qtdialogs.ArmoryDialog import ArmoryDialog

################################################################################
class DlgDisplayTxIn(ArmoryDialog):
   def __init__(self, parent, main, pytxOrUstx, txiIndex, txinListFromBDM=None):
      super(DlgDisplayTxIn, self).__init__(parent, main)

      lblDescr = QRichLabel(self.tr("<center><u><b>TxIn Information</b></u></center>"))

      edtBrowse = QtWidgets.QTextBrowser()
      edtBrowse.setFont(GETFONT('Fixed', 9))
      edtBrowse.setReadOnly(True)
      edtBrowse.setLineWrapMode(QtWidgets.QTextEdit.NoWrap)

      ustx = None
      pytx = pytxOrUstx
      if isinstance(pytx, UnsignedTransaction):
         ustx = pytx
         pytx = ustx.getPyTxSignedIfPossible()

      txin = pytx.inputs[txiIndex]
      scrType  = getTxInScriptType(txin)
      typeName = CPP_TXIN_SCRIPT_NAMES[scrType]
      txHashBE = binary_to_hex(txin.outpoint.txHash, BIGENDIAN)
      txIdxBE  = int_to_hex(txin.outpoint.txOutIndex, 4, BIGENDIAN)
      seqHexBE = int_to_hex(txin.intSeq, 4, BIGENDIAN)
      opStrings = convertScriptToOpStrings(txin.binScript)

      senderAddr = TxInExtractAddrStrIfAvail(txin)
      srcStr = ''
      if not senderAddr:
         senderAddr = self.tr('[[Cannot determine from TxIn Script]]')
      else:
         wltID  = self.main.getWalletForAddrHash(addrStr_to_hash160(senderAddr)[1])
         if wltID:
            wlt = self.main.walletMap[wltID]
            srcStr = self.tr('Wallet "%s" (%s)' % (wlt.labelName, wlt.uniqueIDB58))
         else:
            lbox = self.main.getLockboxByP2SHAddrStr(senderAddr)
            if lbox:
               srcStr = self.tr('Lockbox %d-of-%d "%s" (%s)' % (lbox.M, lbox.N, lbox.shortName, lbox.uniqueIDB58))



      dispLines = []
      dispLines.append(self.tr('<font size=4><u><b>Information on TxIn</b></u></font>:'))
      dispLines.append(self.tr('   <b>TxIn Index:</b>         %s' % txiIndex))
      dispLines.append(self.tr('   <b>TxIn Spending:</b>      %s:%s' % (txHashBE, txIdxBE)))
      dispLines.append(self.tr('   <b>TxIn Sequence</b>:      0x%s' % seqHexBE))
      if len(txin.binScript)>0:
         dispLines.append(self.tr('   <b>TxIn Script Type</b>:   %s' % typeName))
         dispLines.append(self.tr('   <b>TxIn Source</b>:        %s' % senderAddr))
         if srcStr:
            dispLines.append(self.tr('   <b>TxIn Wallet</b>:        %s' % srcStr))
         dispLines.append(self.tr('   <b>TxIn Script</b>:'))
         for op in opStrings:
            dispLines.append('      %s' % op)

      wltID = ''
      scrType = getTxInScriptType(txin)
      if txinListFromBDM and len(txinListFromBDM[txiIndex][0])>0:

         # We had a BDM to help us get info on each input -- use it
         scrAddr,val,blk,hsh,idx,script = txinListFromBDM[txiIndex]
         scrType = getTxOutScriptType(script)

         dispInfo = self.main.getDisplayStringForScript(script, 100, prefIDOverAddr=True)
         #print dispInfo
         addrStr = dispInfo['String']
         wltID   = dispInfo['WltID']
         if not wltID:
            wltID  = dispInfo['LboxID']
         if not wltID:
            wltID = ''

         wouldBeAddrStr = dispInfo['AddrStr']


         dispLines.append('')
         dispLines.append('')
         dispLines.append(self.tr('<font size=4><u><b>Information on TxOut being spent by this TxIn</b></u></font>:'))
         dispLines.append(self.tr('   <b>Tx Hash:</b>            %s' % txHashBE))
         dispLines.append(self.tr('   <b>Tx Out Index:</b>       %s' % txin.outpoint.txOutIndex))
         dispLines.append(self.tr('   <b>Tx in Block#:</b>       %s' % str(blk)))
         dispLines.append(self.tr('   <b>TxOut Value:</b>        %s' % coin2strNZS(val)))
         dispLines.append(self.tr('   <b>TxOut Script Type:</b>  %s' % CPP_TXOUT_SCRIPT_NAMES[scrType]))
         dispLines.append(self.tr('   <b>TxOut Address:</b>      %s' % wouldBeAddrStr))
         if wltID:
            dispLines.append(self.tr('   <b>TxOut Wallet:</b>       %s' % dispInfo['String']))
         dispLines.append(self.tr('   <b>TxOUt Script:</b>'))
         opStrings = convertScriptToOpStrings(script)
         for op in opStrings:
            dispLines.append('      %s' % op)

      u_string = u""
      for dline in dispLines:
         u_string = u_string + "<br>" + dline.replace(' ', '&nbsp;')

      edtBrowse.setHtml(u_string)
      btnDone = QtWidgets.QPushButton(self.tr("Ok"))
      btnDone.clicked.connect(self.accept)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(edtBrowse)
      layout.addWidget(makeHorizFrame(['Stretch', btnDone]))
      self.setLayout(layout)
      w,h = tightSizeNChar(edtBrowse, 100)
      self.setMinimumWidth(max(w, 500))
      self.setMinimumHeight(max(20*h, 400))


################################################################################
class DlgDisplayTxOut(ArmoryDialog):
   def __init__(self, parent, main, pytxOrUstx, txoIndex):
      super(DlgDisplayTxOut, self).__init__(parent, main)

      lblDescr = QRichLabel(self.tr("<center><u><b>TxOut Information</b></u></center>"))

      edtBrowse = QtWidgets.QTextBrowser()
      edtBrowse.setFont(GETFONT('Fixed', 9))
      edtBrowse.setReadOnly(True)
      edtBrowse.setLineWrapMode(QtWidgets.QTextEdit.NoWrap)

      ustx = None
      pytx = pytxOrUstx
      if isinstance(pytx, UnsignedTransaction):
         ustx = pytx
         pytx = ustx.getPyTxSignedIfPossible()

      wltID = ''

      dispLines = []

      txout   = pytx.outputs[txoIndex]
      val     = txout.value
      script  = txout.binScript
      scrAddr = script_to_scrAddr(script)
      scrType = getTxOutScriptType(script)

      dispInfo = self.main.getDisplayStringForScript(script, 100, prefIDOverAddr=True)
      #print dispInfo
      addrStr = dispInfo['String']
      wltID   = dispInfo['WltID']
      if not wltID:
         wltID  = dispInfo['LboxID']
      if not wltID:
         wltID = ''

      wouldBeAddrStr = dispInfo['AddrStr']

      dispLines.append(self.tr('<font size=4><u><b>Information on TxOut</b></u></font>:'))
      dispLines.append(self.tr('   <b>Tx Out Index:</b>       %s' % txoIndex))
      dispLines.append(self.tr('   <b>TxOut Value:</b>        %s' % coin2strNZS(val)))
      dispLines.append(self.tr('   <b>TxOut Script Type:</b>  %s' % CPP_TXOUT_SCRIPT_NAMES[scrType]))
      dispLines.append(self.tr('   <b>TxOut Address:</b>      %s' % wouldBeAddrStr))
      if wltID:
         dispLines.append(self.tr('   <b>TxOut Wallet:</b>       %s' % dispInfo['String']))
      else:
         dispLines.append(self.tr('   <b>TxOut Wallet:</b>       [[Unrelated to any loaded wallets]]'))
      dispLines.append(self.tr('   <b>TxOut Script:</b>'))
      opStrings = convertScriptToOpStrings(script)
      for op in opStrings:
         dispLines.append('      %s' % op)

      u_string = u""
      for dline in dispLines:
         if not isinstance(dline, str):
            line_to_str =  str(dline)
         else:
            line_to_str = dline
         u_string = u_string + u"<br>" + line_to_str.replace(u' ', u'&nbsp;')

      edtBrowse.setHtml(u_string)
      btnDone = QtWidgets.QPushButton(self.tr("Ok"))
      btnDone.clicked.connect(self.accept)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(edtBrowse)
      layout.addWidget(makeHorizFrame(['Stretch', btnDone]))
      self.setLayout(layout)
      w,h = tightSizeNChar(edtBrowse, 100)
      self.setMinimumWidth(max(w, 500))
      self.setMinimumHeight(max(20*h, 400))

################################################################################
class DlgDispTxInfo(ArmoryDialog):
   def __init__(self, pytx, wlt, parent, main, mode=None, \
      precomputeIdxGray=None, precomputeAmt=None, txtime=None,
      ledgerEntry=None):
      """
      This got freakin' complicated, because I'm trying to handle
      wallet/nowallet, BDM/noBDM and Std/Adv/Dev all at once.

      We can override the user mode as an input argument, in case a std
      user decides they want to see the tx in adv/dev mode
      """
      super(DlgDispTxInfo, self).__init__(parent, main)
      self.mode = mode

      FIELDS = enum('Hash', 'OutList', 'SumOut', 'InList', 'SumIn', \
                    'Time', 'Blk', 'Idx', 'TxSize', 'TxWeight')
      self.data = extractTxInfo(pytx, txtime)

      # If this is actually a ustx in here...
      ustx = None
      if isinstance(pytx, UnsignedTransaction):
         ustx = pytx
         pytx = ustx.getPyTxSignedIfPossible()


      self.pytx = pytx.copy()

      if self.mode == None:
         self.mode = self.main.usermode

      txHash = self.data[FIELDS.Hash]

      haveWallet = (wlt != None)
      haveBDM = TheBDM.getState() == BDM_BLOCKCHAIN_READY

      # Should try to identify what is change and what's not
      fee = None
      txAmt = self.data[FIELDS.SumOut]

      # Collect our own outputs only, and ID non-std tx
      svPairSelf = []
      svPairOther = []
      indicesSelf = []
      indicesOther = []
      indicesMakeGray = []
      idx = 0
      for scrType, amt, script, msInfo in self.data[FIELDS.OutList]:
         scrAddr = script_to_scrAddr(script)
         if haveWallet and wlt.hasAddrHash(scrAddr):
            svPairSelf.append([scrAddr, amt])
            indicesSelf.append(idx)
         else:
            svPairOther.append([scrAddr, amt])
            indicesOther.append(idx)
         idx += 1

      txdir = None
      changeIndex = None
      svPairDisp = None

      if self.data[FIELDS.SumOut] and self.data[FIELDS.SumIn]:
         fee = self.data[FIELDS.SumOut] - self.data[FIELDS.SumIn]

      if ledgerEntry:
         txAmt = ledgerEntry.value

         if ledgerEntry.sent_to_self:
            txdir = self.tr('Sent-to-Self')
            svPairDisp = []
            if len(self.pytx.outputs)==1:
               txAmt = fee
               triplet = self.data[FIELDS.OutList][0]
               scrAddr = script_to_scrAddr(triplet[2])
               svPairDisp.append([scrAddr, triplet[1]])
            else:
               txAmt, changeIndex = determineSentToSelfAmt(ledgerEntry, wlt)
               for i, triplet in enumerate(self.data[FIELDS.OutList]):
                  if not i == changeIndex:
                     scrAddr = script_to_scrAddr(triplet[2])
                     svPairDisp.append([scrAddr, triplet[1]])
                  else:
                     indicesMakeGray.append(i)
         else:
            if ledgerEntry.value > 0:
               txdir = self.tr('Received')
               svPairDisp = svPairSelf
               indicesMakeGray.extend(indicesOther)
            if ledgerEntry.value < 0:
               txdir = self.tr('Sent')
               svPairDisp = svPairOther
               indicesMakeGray.extend(indicesSelf)
      else:
         '''
         no ledger entry is available for this tx, let's try to figure
         out if it affects us
         '''
         #short-hand in case of USTX
         if ustx is not None:
            svPairDisp = svPairOther
            indicesMakeGray.extend(indicesSelf)
            txAmt = -1 * sum([val[1] for val in svPairOther])

      # If this is a USTX, the above calculation probably didn't do its job
      # It is possible, but it's also possible that this Tx has nothing to
      # do with our wallet, which is not the focus of the above loop/conditions
      # So we choose to pass in the amount we already computed based on extra
      # information available in the USTX structure
      if precomputeAmt:
         txAmt = precomputeAmt


      layout = QtWidgets.QGridLayout()
      lblDescr = QtWidgets.QLabel(self.tr('Transaction Information:'))

      layout.addWidget(lblDescr, 0, 0, 1, 1)

      frm = QtWidgets.QFrame()
      frm.setFrameStyle(STYLE_RAISED)
      frmLayout = QtWidgets.QGridLayout()
      lbls = []



      # Show the transaction ID, with the user's preferred endianness
      # I hate BE, but block-explorer requires it so it's probably a better default
      endianness = TheSettings.getSettingOrSetDefault('PrefEndian', BIGENDIAN)
      estr = ''
      if self.mode in (USERMODE.Advanced, USERMODE.Expert):
         estr = ' (BE)' if endianness == BIGENDIAN else ' (LE)'

      lbls.append([])
      lbls[-1].append(createToolTipWidget(self.tr('Unique identifier for this transaction')))
      lbls[-1].append(QtWidgets.QLabel(self.tr('Transaction ID' )+ estr + ':'))


      # Want to display the hash of the Tx if we have a valid one:
      # A USTX does not have a valid hash until it's completely signed, though
      longTxt = self.tr('[[ Transaction ID cannot be determined without all signatures ]]')
      w, h = relaxedSizeStr(QRichLabel(''), longTxt)

      tempPyTx = self.pytx.copy()
      if ustx:
         finalTx = ustx.getBroadcastTxIfReady()
         if finalTx:
            tempPyTx = finalTx.copy()
         else:
            tempPyTx = None
            lbls[-1].append(QRichLabel(self.tr('<font color="gray"> '
               '[[ Transaction ID cannot be determined without all signatures ]] '
               '</font>')))

      if tempPyTx:
         txHash = binary_to_hex(tempPyTx.getHash(), endOut=endianness)
         lbls[-1].append(QtWidgets.QLabel(txHash))


      lbls[-1][-1].setMinimumWidth(w)

      if self.mode in (USERMODE.Expert,):
         # Add protocol version and locktime to the display
         lbls.append([])
         lbls[-1].append(createToolTipWidget(self.tr('Bitcoin Protocol Version Number')))
         lbls[-1].append(QtWidgets.QLabel(self.tr('Tx Version:')))
         lbls[-1].append(QtWidgets.QLabel(str(self.pytx.version)))

         lbls.append([])
         lbls[-1].append(createToolTipWidget(
            self.tr('The time at which this transaction becomes valid.')))
         lbls[-1].append(QtWidgets.QLabel(self.tr('Lock-Time:')))
         if self.pytx.lockTime == 0:
            lbls[-1].append(QtWidgets.QLabel(self.tr('Immediate (0)')))
         elif self.pytx.lockTime < 500000000:
            lbls[-1].append(QtWidgets.QLabel(self.tr('Block %s' % self.pytx.lockTime)))
         else:
            lbls[-1].append(QtWidgets.QLabel(unixTimeToFormatStr(self.pytx.lockTime)))



      lbls.append([])
      lbls[-1].append(createToolTipWidget(self.tr('Comment stored for this transaction in this wallet')))
      lbls[-1].append(QtWidgets.QLabel(self.tr('User Comment:')))
      try:
         txhash_bin = hex_to_binary(txHash, endOut=endianness)
      except:
         txhash_bin = txHash
      comment_tx = ''
      if haveWallet:
         comment_tx = wlt.getComment(txhash_bin)
         if not comment_tx: # and tempPyTx:
            comment_tx = wlt.getAddrCommentIfAvail(txhash_bin)
            #for txout in tempPyTx.outputs:
             #  script = script_to_scrAddr(txout.getScript())


      if comment_tx:
         lbls[-1].append(QRichLabel(comment_tx))
      else:
         lbls[-1].append(QRichLabel(self.tr('<font color="gray">[None]</font>')))


      if not self.data[FIELDS.Time] == None:
         lbls.append([])
         if self.data[FIELDS.Blk] >= 2 ** 32 - 1:
            lbls[-1].append(createToolTipWidget(
                  self.tr('The time that you computer first saw this transaction')))
         else:
            lbls[-1].append(createToolTipWidget(
                  self.tr('All transactions are eventually included in a "block."  The '
                  'time shown here is the time that the block entered the "blockchain."')))
         lbls[-1].append(QtWidgets.QLabel('Transaction Time:'))
         lbls[-1].append(QtWidgets.QLabel(str(self.data[FIELDS.Time])))

      if not self.data[FIELDS.Blk] == None:
         nConf = 0
         if self.data[FIELDS.Blk] >= 2 ** 32 - 1:
            lbls.append([])
            lbls[-1].append(createToolTipWidget(
                  self.tr('This transaction has not yet been included in a block. '
                  'It usually takes 5-20 minutes for a transaction to get '
                  'included in a block after the user hits the "Send" button.')))
            lbls[-1].append(QtWidgets.QLabel('Block Number:'))
            lbls[-1].append(QRichLabel('<i>Not in the blockchain yet</i>'))
         else:
            idxStr = ''
            if not self.data[FIELDS.Idx] == None and self.mode == USERMODE.Expert:
               idxStr ='  (Tx #%d)' % self.data[FIELDS.Idx]
            lbls.append([])
            lbls[-1].append(createToolTipWidget(
                  self.tr('Every transaction is eventually included in a "block" which '
                  'is where the transaction is permanently recorded.  A new block '
                  'is produced approximately every 10 minutes.')))
            lbls[-1].append(QtWidgets.QLabel('Included in Block:'))
            lbls[-1].append(QRichLabel(str(self.data[FIELDS.Blk]) + idxStr))
            if TheBDM.getState() == BDM_BLOCKCHAIN_READY:
               nConf = TheBDM.getTopBlockHeight() - self.data[FIELDS.Blk] + 1
               lbls.append([])
               lbls[-1].append(createToolTipWidget(
                     self.tr('The number of blocks that have been produced since '
                     'this transaction entered the blockchain.  A transaction '
                     'with 6 or more confirmations is nearly impossible to reverse.')))
               lbls[-1].append(QtWidgets.QLabel(self.tr('Confirmations:')))
               lbls[-1].append(QRichLabel(str(nConf)))

      isRBF = self.pytx.isRBF()
      if isRBF:
         lbls.append([])
         lbls[-1].append(createToolTipWidget(
               self.tr('This transaction can be replaced by another transaction that '
               'spends the same inputs if the replacement transaction has '
               'a higher fee.')))
         lbls[-1].append(QtWidgets.QLabel(self.tr('Mempool Replaceable: ')))
         lbls[-1].append(QRichLabel(str(isRBF)))


      if svPairDisp == None and precomputeAmt == None:
         # Couldn't determine recip/change outputs
         lbls.append([])
         lbls[-1].append(createToolTipWidget(
               self.tr('Most transactions have at least a recipient output and a '
               'returned-change output.  You do not have enough information '
               'to determine which is which, and so this fields shows the sum '
               'of <b>all</b> outputs.')))
         lbls[-1].append(QtWidgets.QLabel(self.tr('Sum of Outputs:')))
         lbls[-1].append(QtWidgets.QLabel(coin2str(txAmt, maxZeros=1).strip() + '  BTC'))
      else:
         lbls.append([])
         lbls[-1].append(createToolTipWidget(
               self.tr('Bitcoins were either sent or received, or sent-to-self')))
         lbls[-1].append(QtWidgets.QLabel('Transaction Direction:'))
         lbls[-1].append(QRichLabel(txdir))

         lbls.append([])
         lbls[-1].append(createToolTipWidget(
               self.tr('The value shown here is the net effect on your '
               'wallet, including transaction fee.')))
         lbls[-1].append(QtWidgets.QLabel('Transaction Amount:'))
         lbls[-1].append(QRichLabel(coin2str(txAmt, maxZeros=1).strip() + '  BTC'))
         if txAmt < 0:
            lbls[-1][-1].setText('<font color="red">' + lbls[-1][-1].text() + '</font> ')
         elif txAmt > 0:
            lbls[-1][-1].setText('<font color="green">' + lbls[-1][-1].text() + '</font> ')


      if not self.data[FIELDS.TxSize] == None:
         txsize = str(self.data[FIELDS.TxSize])
         txsize_str = self.tr("%s bytes" % txsize)
         lbls.append([])
         lbls[-1].append(createToolTipWidget(
            self.tr('Size of the transaction in bytes')))
         lbls[-1].append(QtWidgets.QLabel(self.tr('Tx Size: ')))
         lbls[-1].append(QtWidgets.QLabel(txsize_str))

      if not self.data[FIELDS.SumIn] == None:
         fee = self.data[FIELDS.SumIn] - self.data[FIELDS.SumOut]
         lbls.append([])
         lbls[-1].append(createToolTipWidget(
            self.tr('Transaction fees go to users supplying the Bitcoin network with '
            'computing power for processing transactions and maintaining security.')))
         lbls[-1].append(QtWidgets.QLabel('Tx Fee Paid:'))

         fee_str = coin2str(fee, maxZeros=0).strip() + '  BTC'
         if not self.data[FIELDS.TxWeight] == None:
            fee_byte = float(fee) / float(self.data[FIELDS.TxWeight])
            fee_str += ' (%d sat/B)' % fee_byte

         lbls[-1].append(QtWidgets.QLabel(fee_str))





      lastRow = 0
      for row, lbl3 in enumerate(lbls):
         lastRow = row
         for i in range(3):
            lbl3[i].setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            lbl3[i].setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                            QtCore.Qt.TextSelectableByKeyboard)
         frmLayout.addWidget(lbl3[0], row, 0, 1, 1)
         frmLayout.addWidget(lbl3[1], row, 1, 1, 1)
         frmLayout.addWidget(lbl3[2], row, 3, 1, 2)

      spacer = QtWidgets.QSpacerItem(20, 20)
      frmLayout.addItem(spacer, 0, 2, len(lbls), 1)

      # Show the list of recipients, if possible
      numShow = 3
      rlbls = []
      if svPairDisp is not None:
         numRV = len(svPairDisp)
         for i, sv in enumerate(svPairDisp):
            rlbls.append([])
            if i == 0:
               rlbls[-1].append(createToolTipWidget(
                  self.tr('All outputs of the transaction <b>excluding</b> change-'
                  'back-to-sender outputs.  If this list does not look '
                  'correct, it is possible that the change-output was '
                  'detected incorrectly -- please check the complete '
                  'input/output list below.')))
               rlbls[-1].append(QtWidgets.QLabel(self.tr('Recipients:')))
            else:
               rlbls[-1].extend([QtWidgets.QLabel(), QtWidgets.QLabel()])

            rlbls[-1].append(QtWidgets.QLabel(scrAddr_to_addrStr(sv[0])))
            if numRV > 1:
               rlbls[-1].append(QtWidgets.QLabel(coin2str(sv[1], maxZeros=1) + '  BTC'))
            else:
               rlbls[-1].append(QtWidgets.QLabel(''))
            ffixBold = GETFONT('Fixed', 10)
            ffixBold.setWeight(QtGui.QFont.Bold)
            rlbls[-1][-1].setFont(ffixBold)

            if numRV > numShow and i == numShow - 2:
               moreStr = self.tr('[%s more recipients]' % (numRV - numShow + 1))
               rlbls.append([])
               rlbls[-1].extend([QtWidgets.QLabel(), QtWidgets.QLabel(), QtWidgets.QLabel(moreStr), QtWidgets.QLabel()])
               break


         # ##
         for i, lbl4 in enumerate(rlbls):
            for j in range(4):
               lbl4[j].setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                            QtCore.Qt.TextSelectableByKeyboard)
            row = lastRow + 1 + i
            frmLayout.addWidget(lbl4[0], row, 0, 1, 1)
            frmLayout.addWidget(lbl4[1], row, 1, 1, 1)
            frmLayout.addWidget(lbl4[2], row, 3, 1, 1)
            frmLayout.addWidget(lbl4[3], row, 4, 1, 1)



      # TxIns/Senders
      wWlt = relaxedSizeStr(GETFONT('Var'), 'A' * 10)[0]
      wAddr = relaxedSizeStr(GETFONT('Var'), 'A' * 31)[0]
      wAmt = relaxedSizeStr(GETFONT('Fixed'), 'A' * 20)[0]
      if ustx:
         self.txInModel = TxInDispModel(ustx, self.data[FIELDS.InList], self.main)
      else:
         self.txInModel = TxInDispModel(pytx, self.data[FIELDS.InList], self.main)
      self.txInView = QtWidgets.QTableView()
      self.txInView.setModel(self.txInModel)
      self.txInView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
      self.txInView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
      self.txInView.horizontalHeader().setStretchLastSection(True)
      self.txInView.verticalHeader().setDefaultSectionSize(20)
      self.txInView.verticalHeader().hide()
      w, h = tightSizeNChar(self.txInView, 1)
      self.txInView.setMinimumHeight(2 * (1.4 * h))
      #self.txInView.setMaximumHeight(5 * (1.4 * h))
      self.txInView.hideColumn(TXINCOLS.OutPt)
      self.txInView.hideColumn(TXINCOLS.OutIdx)
      self.txInView.hideColumn(TXINCOLS.Script)
      self.txInView.hideColumn(TXINCOLS.AddrStr)

      if self.mode == USERMODE.Standard:
         initialColResize(self.txInView, [wWlt, wAddr, wAmt, 0, 0, 0, 0, 0, 0])
         self.txInView.hideColumn(TXINCOLS.FromBlk)
         self.txInView.hideColumn(TXINCOLS.ScrType)
         self.txInView.hideColumn(TXINCOLS.Sequence)
         # self.txInView.setSelectionMode(QtWidgets.QTableView.NoSelection)
      elif self.mode == USERMODE.Advanced:
         initialColResize(self.txInView, [0.8 * wWlt, 0.6 * wAddr, wAmt, 0, 0, 0, 0.2, 0, 0])
         self.txInView.hideColumn(TXINCOLS.FromBlk)
         self.txInView.hideColumn(TXINCOLS.Sequence)
         # self.txInView.setSelectionMode(QtWidgets.QTableView.NoSelection)
      elif self.mode == USERMODE.Expert:
         self.txInView.resizeColumnsToContents()

      self.txInView.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
      self.txInView.customContextMenuRequested.connect(self.showContextMenuTxIn)

      # List of TxOuts/Recipients
      if not precomputeIdxGray is None:
         indicesMakeGray = precomputeIdxGray[:]
      self.txOutModel = TxOutDispModel(self.pytx, self.main, idxGray=indicesMakeGray)
      self.txOutView = QtWidgets.QTableView()
      self.txOutView.setModel(self.txOutModel)
      self.txOutView.setSelectionBehavior(QtWidgets.QTableView.SelectRows)
      self.txOutView.setSelectionMode(QtWidgets.QTableView.SingleSelection)
      self.txOutView.verticalHeader().setDefaultSectionSize(20)
      self.txOutView.verticalHeader().hide()
      self.txOutView.setMinimumHeight(2 * (1.3 * h))
      #self.txOutView.setMaximumHeight(5 * (1.3 * h))
      initialColResize(self.txOutView, [wWlt, 0.8 * wAddr, wAmt, 0.25, 0])
      self.txOutView.hideColumn(TXOUTCOLS.Script)
      self.txOutView.hideColumn(TXOUTCOLS.AddrStr)
      if self.mode == USERMODE.Standard:
         self.txOutView.hideColumn(TXOUTCOLS.ScrType)
         initialColResize(self.txOutView, [wWlt, wAddr, 0.25, 0, 0])
         self.txOutView.horizontalHeader().setStretchLastSection(True)
         # self.txOutView.setSelectionMode(QtWidgets.QTableView.NoSelection)
      elif self.mode == USERMODE.Advanced:
         initialColResize(self.txOutView, [0.8 * wWlt, 0.6 * wAddr, wAmt, 0.25, 0])
         # self.txOutView.setSelectionMode(QtWidgets.QTableView.NoSelection)
      elif self.mode == USERMODE.Expert:
         initialColResize(self.txOutView, [wWlt, wAddr, wAmt, 0.25, 0])
      # self.txOutView.resizeColumnsToContents()

      self.txOutView.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
      self.txOutView.customContextMenuRequested.connect(self.showContextMenuTxOut)

      self.lblTxioInfo = QRichLabel('')
      self.lblTxioInfo.setMinimumWidth(tightSizeNChar(self.lblTxioInfo, 30)[0])
      #self.txInView.clicked.connect(lambda: self.dispTxioInfo('In'))
      #self.txOutView.clicked.connect(lambda: self.dispTxioInfo('Out'))
      self.txInView.doubleClicked.connect(self.showTxInDialog)
      self.txOutView.doubleClicked.connect(self.showTxOutDialog)

      # scrFrm = QtWidgets.QFrame()
      # scrFrm.setFrameStyle(STYLE_SUNKEN)
      # scrFrmLayout = Q


      self.scriptArea = QtWidgets.QScrollArea()
      self.scriptArea.setWidget(self.lblTxioInfo)
      self.scriptFrm = makeLayoutFrame(HORIZONTAL, [self.scriptArea])
      # self.scriptFrm.setMaximumWidth(150)
      self.scriptArea.setMaximumWidth(200)

      self.frmIOList = QtWidgets.QFrame()
      self.frmIOList.setFrameStyle(STYLE_SUNKEN)
      frmIOListLayout = QtWidgets.QGridLayout()

      lblInputs = QtWidgets.QLabel(self.tr('Transaction Inputs (Sending addresses):'))
      ttipText = (self.tr('All transactions require previous transaction outputs as inputs.'))
      if not haveBDM:
         ttipText += (self.tr('<b>Since the blockchain is not available, not all input '
                      'information is available</b>.  You need to view this '
                      'transaction on a system with an internet connection '
                      '(and blockchain) if you want to see the complete information.'))
      else:
         ttipText += (self.tr('Each input is like an X amount dollar bill.  Usually there are more inputs '
                      'than necessary for the transaction, and there will be an extra '
                      'output returning change to the sender'))
      ttipInputs = createToolTipWidget(ttipText)

      lblOutputs = QtWidgets.QLabel(self.tr('Transaction Outputs (Receiving addresses):'))
      ttipOutputs = createToolTipWidget(
                  self.tr('Shows <b>all</b> outputs, including other recipients '
                  'of the same transaction, and change-back-to-sender outputs '
                  '(change outputs are displayed in light gray).'))

      self.lblChangeDescr = QRichLabel( self.tr('Some outputs might be "change."'), doWrap=False)
      self.lblChangeDescr.setOpenExternalLinks(True)



      inStrip = makeLayoutFrame(HORIZONTAL, [lblInputs, ttipInputs, STRETCH])
      outStrip = makeLayoutFrame(HORIZONTAL, [lblOutputs, ttipOutputs, STRETCH])

      frmIOListLayout.addWidget(inStrip, 0, 0, 1, 1)
      frmIOListLayout.addWidget(self.txInView, 1, 0, 1, 1)
      frmIOListLayout.addWidget(outStrip, 2, 0, 1, 1)
      frmIOListLayout.addWidget(self.txOutView, 3, 0, 1, 1)
      # frmIOListLayout.addWidget(self.lblTxioInfo, 0,1, 4,1)
      self.frmIOList.setLayout(frmIOListLayout)


      self.btnIOList = QtWidgets.QPushButton('')
      self.btnCopy = QtWidgets.QPushButton(self.tr('Copy Raw Tx (Hex)'))
      self.lblCopied = QRichLabel('')
      self.btnOk = QtWidgets.QPushButton(self.tr('OK'))
      self.btnIOList.setCheckable(True)
      self.btnIOList.clicked.connect(self.extraInfoClicked)
      self.btnOk.clicked.connect(self.accept)
      self.btnCopy.clicked.connect(self.copyRawTx)

      btnStrip = makeHorizFrame([self.btnIOList,
                                 self.btnCopy,
                                 self.lblCopied,
                                 'Stretch',
                                 self.lblChangeDescr,
                                 'Stretch',
                                 self.btnOk])

      if not self.mode == USERMODE.Expert:
         self.btnCopy.setVisible(False)


      if self.mode == USERMODE.Standard:
         self.btnIOList.setChecked(False)
      else:
         self.btnIOList.setChecked(True)
      self.extraInfoClicked()


      frm.setLayout(frmLayout)
      layout.addWidget(frm, 2, 0, 1, 1)
      layout.addWidget(self.scriptArea, 2, 1, 1, 1)
      layout.addWidget(self.frmIOList, 3, 0, 1, 2)
      layout.addWidget(btnStrip, 4, 0, 1, 2)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Transaction Info'))



   def extraInfoClicked(self):
      if self.btnIOList.isChecked():
         self.frmIOList.setVisible(True)
         self.btnCopy.setVisible(True)
         self.lblCopied.setVisible(True)
         self.btnIOList.setText(self.tr('<<< Less Info'))
         self.lblChangeDescr.setVisible(True)
         self.scriptArea.setVisible(False) # self.mode == USERMODE.Expert)
         # Disabling script area now that you can double-click to get it
      else:
         self.frmIOList.setVisible(False)
         self.scriptArea.setVisible(False)
         self.btnCopy.setVisible(False)
         self.lblCopied.setVisible(False)
         self.lblChangeDescr.setVisible(False)
         self.btnIOList.setText(self.tr('Advanced >>>'))

   def dispTxioInfo(self, InOrOut):
      hexScript = None
      headStr = None
      if InOrOut == 'In':
         selection = self.txInView.selectedIndexes()
         if len(selection) == 0:
            return
         row = selection[0].row()
         hexScript = str(self.txInView.model().index(row, TXINCOLS.Script).data().toString())
         headStr = self.tr('TxIn Script:')
      elif InOrOut == 'Out':
         selection = self.txOutView.selectedIndexes()
         if len(selection) == 0:
            return
         row = selection[0].row()
         hexScript = str(self.txOutView.model().index(row, TXOUTCOLS.Script).data().toString())
         headStr = self.tr('TxOut Script:')


      if hexScript:
         binScript = hex_to_binary(hexScript)
         addrStr = None
         scrType = getTxOutScriptType(binScript)
         if scrType in CPP_TXOUT_HAS_ADDRSTR:
            addrStr = script_to_addrStr(binScript)

         oplist = convertScriptToOpStrings(hex_to_binary(hexScript))
         opprint = []
         prevOpIsPushData = False
         for op in oplist:

            if addrStr is None or not prevOpIsPushData:
               opprint.append(op)
            else:
               opprint.append(op + ' <font color="gray">(%s)</font>' % addrStr)
               prevOpIsPushData = False

            if 'pushdata' in op.lower():
               prevOpIsPushData = True

         lblScript = QRichLabel('')
         lblScript.setText('<b>Script:</b><br><br>' + '<br>'.join(opprint))
         lblScript.setWordWrap(False)
         lblScript.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                        QtCore.Qt.TextSelectableByKeyboard)

         self.scriptArea.setWidget(makeLayoutFrame(VERTICAL, [lblScript]))
         self.scriptArea.setMaximumWidth(200)


   def copyRawTx(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      #print "Binscript: " + binary_to_hex(self.pytx.inputs[0].binScript)
      clipb.setText(binary_to_hex(self.pytx.serialize()))
      self.lblCopied.setText(self.tr('<i>Copied to Clipboard!</i>'))


   #############################################################################
   def showTxInDialog(self, *args):
      # I really should've just used a dictionary instead of list with enum indices
      FIELDS = enum('Hash', 'OutList', 'SumOut', 'InList', 'SumIn', 'Time', 'Blk', 'Idx')
      try:
         idx = self.txInView.selectedIndexes()[0].row()
         DlgDisplayTxIn(self, self.main, self.pytx, idx, self.data[FIELDS.InList]).exec_()
      except:
         LOGEXCEPT('Error showing TxIn')

   #############################################################################
   def showTxOutDialog(self, *args):
      # I really should've just used a dictionary instead of list with enum indices
      FIELDS = enum('Hash', 'OutList', 'SumOut', 'InList', 'SumIn', 'Time', 'Blk', 'Idx')
      try:
         idx = self.txOutView.selectedIndexes()[0].row()
         DlgDisplayTxOut(self, self.main, self.pytx, idx).exec_()
      except:
         LOGEXCEPT('Error showing TxOut')

   #############################################################################
   def showContextMenuTxIn(self, pos):
      menu = QtWidgets.QMenu(self.txInView)
      std = (self.main.usermode == USERMODE.Standard)
      adv = (self.main.usermode == USERMODE.Advanced)
      dev = (self.main.usermode == USERMODE.Expert)

      if True:   actCopySender = menu.addAction(self.tr("Copy Sender Address"))
      if True:   actCopyWltID = menu.addAction(self.tr("Copy Wallet ID"))
      if True:   actCopyAmount = menu.addAction(self.tr("Copy Amount"))
      if True:   actMoreInfo = menu.addAction(self.tr("More Info"))
      idx = self.txInView.selectedIndexes()[0]
      action = menu.exec_(QtGui.QCursor.pos())

      if action == actMoreInfo:
         self.showTxInDialog()
      if action == actCopyWltID:
         s = str(self.txInView.model().index(idx.row(), TXINCOLS.WltID).data().toString())
      elif action == actCopySender:
         s = str(self.txInView.model().index(idx.row(), TXINCOLS.Sender).data().toString())
      elif action == actCopyAmount:
         s = str(self.txInView.model().index(idx.row(), TXINCOLS.Btc).data().toString())
      #elif dev and action == actCopyOutPt:
         #s1 = str(self.txInView.model().index(idx.row(), TXINCOLS.OutPt).data().toString())
         #s2 = str(self.txInView.model().index(idx.row(), TXINCOLS.OutIdx).data().toString())
         #s = s1 + ':' + s2
      #elif dev and action == actCopyScript:
         #s = str(self.txInView.model().index(idx.row(), TXINCOLS.Script).data().toString())
      else:
         return

      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(s.strip())

   #############################################################################
   def showContextMenuTxOut(self, pos):
      menu = QtWidgets.QMenu(self.txOutView)
      std = (self.main.usermode == USERMODE.Standard)
      adv = (self.main.usermode == USERMODE.Advanced)
      dev = (self.main.usermode == USERMODE.Expert)

      if True:   actCopyRecip  = menu.addAction(self.tr("Copy Recipient Address"))
      if True:   actCopyWltID  = menu.addAction(self.tr("Copy Wallet ID"))
      if True:   actCopyAmount = menu.addAction(self.tr("Copy Amount"))
      if dev:    actCopyScript = menu.addAction(self.tr("Copy Raw Script"))
      idx = self.txOutView.selectedIndexes()[0]
      action = menu.exec_(QtGui.QCursor.pos())

      if action == actCopyWltID:
         s = self.txOutView.model().index(idx.row(), TXOUTCOLS.WltID).data().toString()
      elif action == actCopyRecip:
         s = self.txOutView.model().index(idx.row(), TXOUTCOLS.AddrStr).data().toString()
      elif action == actCopyAmount:
         s = self.txOutView.model().index(idx.row(), TXOUTCOLS.Btc).data().toString()
      elif dev and action == actCopyScript:
         s = self.txOutView.model().index(idx.row(), TXOUTCOLS.Script).data().toString()
      else:
         return

      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(str(s).strip())

################################################################################
def extractTxInfo(pytx, rcvTime=None):
   ustx = None
   if isinstance(pytx, UnsignedTransaction):
      ustx = pytx
      pytx = ustx.pytxObj

   txHash = pytx.getHash()
   try:
      hasTxHash = len(txHash) == 32
   except TypeError:
      hasTxHash = False
   txSize, txWeight, sumTxIn, txTime, txBlk, txIdx = [None] * 6

   txOutToList = pytx.makeRecipientsList()
   sumTxOut = sum([t[1] for t in txOutToList])

   if TheBDM.getState() == BDM_BLOCKCHAIN_READY and hasTxHash:
      txProto = TheBridge.service.getTxByHash(txHash)
      if txProto is not None:
         hgt = txProto.height
         txWeight = pytx.getTxWeight()
         if hgt <= TheBDM.getTopBlockHeight():
            header = PyBlockHeader()
            header.unserialize(TheBridge.service.getHeaderByHeight(hgt))
            txTime = unixTimeToFormatStr(header.timestamp)
            txBlk = hgt
            txIdx = txProto.tx_index
            txSize = pytx.getSize()
         else:
            if rcvTime == None:
               txTime = 'Unknown'
            elif rcvTime == -1:
               txTime = '[[Not broadcast yet]]'
            elif isinstance(rcvTime, str):
               txTime = rcvTime
            else:
               txTime = unixTimeToFormatStr(rcvTime)
            txBlk = UINT32_MAX
            txIdx = -1

   txinFromList = []
   if TheBDM.getState() == BDM_BLOCKCHAIN_READY and pytx.isInitialized():
      haveAllInput = True
      for i in range(pytx.getNumTxIn()):
         txinFromList.append([])
         txin = pytx.getTxIn(i)
         prevTxHash = txin.getOutPoint().txHash
         prevTxIndex = txin.getOutPoint().txOutIndex
         prevTxRaw = TheBridge.service.getTxByHash(prevTxHash)
         if prevTxRaw != None:
            prevTx = PyTx().unserialize(prevTxRaw.raw)
            prevTxOut = prevTx.getTxOut(prevTxIndex)
            txinFromList[-1].append(prevTxOut.getScrAddressStr())
            txinFromList[-1].append(prevTxOut.getValue())
            if prevTx.isInitialized():
               txinFromList[-1].append(prevTxRaw.height)
               txinFromList[-1].append(prevTxHash)
               txinFromList[-1].append(prevTxRaw.tx_index)
               txinFromList[-1].append(prevTxOut.getScript())
            else:
               LOGERROR('How did we get a bad parent pointer? (extractTxInfo)')
               #prevTxOut.pprint()
               txinFromList[-1].append('')
               txinFromList[-1].append('')
               txinFromList[-1].append('')
               txinFromList[-1].append('')
         else:
            haveAllInput = False
            try:
               scraddr = addrStr_to_scrAddr(TxInExtractAddrStrIfAvail(txin))
            except:
               pass

            txinFromList[-1].append(scraddr)
            txinFromList[-1].append('')
            txinFromList[-1].append('')
            txinFromList[-1].append('')
            txinFromList[-1].append('')
            txinFromList[-1].append('')

   elif ustx is not None:
      haveAllInput = True
      for ustxi in ustx.ustxInputs:
         txinFromList.append([])
         txinFromList[-1].append(script_to_scrAddr(ustxi.txoScript))
         txinFromList[-1].append(ustxi.value)
         txinFromList[-1].append('')
         txinFromList[-1].append(hash256(ustxi.supportTx))
         txinFromList[-1].append(ustxi.outpoint.txOutIndex)
         txinFromList[-1].append(ustxi.txoScript)
   else:  # BDM is not initialized
      haveAllInput = False
      for i, txin in enumerate(pytx.inputs):
         scraddr = addrStr_to_scrAddr(TxInExtractAddrStrIfAvail(txin))
         txinFromList.append([])
         txinFromList[-1].append(scraddr)
         txinFromList[-1].append('')
         txinFromList[-1].append('')
         txinFromList[-1].append('')
         txinFromList[-1].append('')
         txinFromList[-1].append('')

   if haveAllInput:
      sumTxIn = sum([x[1] for x in txinFromList])
   else:
      sumTxIn = None

   return [txHash, txOutToList, sumTxOut, txinFromList, sumTxIn, \
      txTime, txBlk, txIdx, txSize, txWeight]
