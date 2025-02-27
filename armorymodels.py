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

from os import path
import os
import platform
import sys
from copy import deepcopy

from qtpy import QtCore, QtGui, QtWidgets
from armoryengine.ArmoryUtils import enum, coin2str, binary_to_hex, \
   int_to_hex, CPP_TXOUT_SCRIPT_NAMES, CPP_TXOUT_MULTISIG, \
   CPP_TXIN_SCRIPT_NAMES, ADDRBYTE, P2SHBYTE
from armoryengine.UserAddressUtils import getDisplayStringForScriptImpl
from armoryengine.Transaction import UnsignedTransaction, \
   getTxInScriptType
from armoryengine.MultiSigUtils import calcLockboxID
from armoryengine.Timer import TimeThisFunction
from armoryengine.BDM import TheBDM, BDM_BLOCKCHAIN_READY
from armoryengine.CppBridge import TheBridge
from armoryengine.AddressUtils import Hash160ToScrAddr, addrStr_to_hash160, \
   scrAddr_to_addrStr
from armoryengine.Settings import TheSettings
from armoryengine.WalletUtils import WalletTypes, determineWalletType

from armorycolors import Colors
from qtdialogs.qtdefines import GETFONT, CHANGE_ADDR_DESCR_STRING

from qtdialogs.ArmoryDialog import ArmoryDialog

WLTVIEWCOLS = enum('Visible', 'ID', 'Name', 'Secure', 'Bal')
LEDGERCOLS  = enum('NumConf', 'UnixTime', 'DateStr', 'TxDir', 'WltName', 'Comment', \
                   'Amount', 'isOther', 'WltID', 'TxHash', 'isCoinbase', 'toSelf', \
                   'optInRBF', 'isChainedZC')
ADDRESSCOLS  = enum('ChainIdx', 'Address', 'Comment', 'NumTx', 'Balance')
ADDRBOOKCOLS = enum('Address', 'WltID', 'NumSent', 'Comment')

TXINCOLS  = enum('WltID', 'Sender', 'Btc', 'OutPt', 'OutIdx', 'FromBlk', \
                               'ScrType', 'Sequence', 'Script', 'AddrStr')
TXOUTCOLS = enum('WltID', 'Recip', 'Btc', 'ScrType', 'Script', 'AddrStr')
PROMCOLS = enum('PromID', 'Label', 'PayAmt', 'FeeAmt')

PAGE_LOAD_OFFSET = 10

################################################################################
class AllWalletsDispModel(QtCore.QAbstractTableModel):

   # The columns enumeration
   def __init__(self, wallets):
      super(AllWalletsDispModel, self).__init__()
      self.wallets = wallets

   def rowCount(self, index=QtCore.QModelIndex()):
      return self.wallets.count()

   def columnCount(self, index=QtCore.QModelIndex()):
      return 5

   def data(self, index, role=QtCore.Qt.DisplayRole):
      bdmState = TheBDM.getState()
      COL = WLTVIEWCOLS
      row,col = index.row(), index.column()
      wlt = self.wallets.getByIndex(row)
      wltID = wlt.walletId
      mainWnd = self.wallets.parent

      if role==QtCore.Qt.DisplayRole:
         if col==COL.Visible:
            return self.wallets.isVisible(row)
         elif col==COL.ID:
            return str(wltID)
         elif col==COL.Name:
            return str(wlt.labelName.ljust(32))
         elif col==COL.Secure:
            wtype,typestr = determineWalletType(wlt, mainWnd)
            return str(typestr)
         elif col==COL.Bal:
            if not bdmState==BDM_BLOCKCHAIN_READY:
               return str('(...)')
            if wlt.isEnabled == True:
               bal = wlt.getBalance('Total')
               if bal==-1:
                  return str('(...)')
               else:
                  dispStr = coin2str(bal, maxZeros=2)
                  return str(dispStr)
            else:
               dispStr = 'Scanning: %d%%' % (mainWnd.walletSideScanProgress[wltID])
               return str(dispStr)

      elif role==QtCore.Qt.TextAlignmentRole:
         if col in (COL.ID, COL.Name):
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         elif col in (COL.Secure,):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         elif col in (COL.Bal,):
            if not bdmState==BDM_BLOCKCHAIN_READY:
               return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
            else:
               return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.BackgroundColorRole:
         t = determineWalletType(wlt, mainWnd)[0]
         if t==WalletTypes.WatchOnly:
            return Colors.TblWltOther
         elif t==WalletTypes.Offline:
            return Colors.TblWltOffline
         else:
            return Colors.TblWltMine
      elif role==QtCore.Qt.FontRole:
         if col==COL.Bal:
            return GETFONT('Fixed')
      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      colLabels = ['', self.tr('ID'), self.tr('Wallet Name'), self.tr('Security'), self.tr('Balance')]
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            return colLabels[section]
      elif role==QtCore.Qt.TextAlignmentRole:
         return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

   def flags(self, index, role=QtCore.Qt.DisplayRole):
      if role == QtCore.Qt.DisplayRole:
         wlt = self.wallets.getByIndex(index.row())

         rowFlag = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable
         if wlt.isEnabled is False:
            return QtCore.Qt.ItemFlags()
         return rowFlag

   def reset(self):
      self.beginResetModel()
      self.endResetModel()

################################################################################
class AllWalletsCheckboxDelegate(QtWidgets.QStyledItemDelegate):
   """
   Taken from http://stackoverflow.com/a/3366899/1610471
   """
   EYESIZE = 20

   def __init__(self, parent=None):
      super(AllWalletsCheckboxDelegate, self).__init__(parent)

   #############################################################################
   def paint(self, painter, option, index):
      bgcolor = QtGui.QPalette().color(
         QtGui.QPalette.Active, QtGui.QPalette.Window)
      if option.state & QtWidgets.QStyle.State_Selected:
         bgcolor = QtWidgets.QApplication.palette().highlight().color()

      if index.column() == WLTVIEWCOLS.Visible:
         isVisible = index.model().data(index)
         image=None
         painter.fillRect(option.rect, bgcolor)
         if isVisible:
            image = QtGui.QImage('./img/visible2.png').scaled(self.EYESIZE,self.EYESIZE)
            pixmap = QtGui.QPixmap.fromImage(image)
            painter.drawPixmap(option.rect, pixmap)
      else:
         QtWidgets.QStyledItemDelegate.paint(self, painter, option, index)

   #############################################################################
   def sizeHint(self, option, index):
      if index.column()==WLTVIEWCOLS.Visible:
         return QtCore.QSize(self.EYESIZE,self.EYESIZE)
      return QtWidgets.QStyledItemDelegate.sizeHint(self, option, index)

################################################################################
class TableEntry():
   def __init__(self, rawData, id=-1, table=[]):
      self.rawData = rawData
      self.id = id
      self.table = table

################################################################################
class LedgerDispModelSimple(QtCore.QAbstractTableModel):
   """ Displays an Nx10 table of pre-formatted/processed ledger entries """
   def __init__(self, ledgerTable, parent=None, main=None, isLboxModel=False):
      super(LedgerDispModelSimple, self).__init__()
      self.parent = parent
      self.main   = main
      self.isLboxModel = isLboxModel

      self.bottomPage = TableEntry(None, 1, [])
      self.currentPage = TableEntry(None, 0, [])
      self.topPage = TableEntry(None, -1, [])

      self.getPageLedger = None
      self.convertLedger = None

      self.ledger = ledgerTable
      self.ledgerDelegateId = None

   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.ledger)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 13

   def data(self, index, role=QtCore.Qt.DisplayRole):
      COL = LEDGERCOLS
      row,col = index.row(), index.column()
      rowData = self.ledger[row]
      nConf = rowData[LEDGERCOLS.NumConf]
      wltID = rowData[LEDGERCOLS.WltID]
      wlt = self.main.wallets.get(wltID)
      optInRBF = rowData[LEDGERCOLS.optInRBF]
      isChainedZC = rowData[LEDGERCOLS.isChainedZC]
      amount = float(rowData[LEDGERCOLS.Amount])
      toSelf = rowData[LEDGERCOLS.toSelf]

      flagged = (optInRBF) and amount < 0 or toSelf
      highlighted = optInRBF or isChainedZC

      if wlt:
         wtype = determineWalletType(wlt, self.main)[0]
      else:
         wtype = WalletTypes.WatchOnly

      if role==QtCore.Qt.DisplayRole:
         return str(rowData[col])
      elif role==QtCore.Qt.TextAlignmentRole:
         if col in (COL.NumConf,  COL.TxDir):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         elif col in (COL.Comment, COL.DateStr):
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         elif col in (COL.Amount,):
            return int(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.DecorationRole:
         pass
      elif role==QtCore.Qt.BackgroundColorRole:
         if optInRBF is True:
            if flagged:
               return Colors.myRBF
            else:
               return Colors.optInRBF
         elif isChainedZC is True:
            return Colors.chainedZC
         elif wtype==WalletTypes.WatchOnly:
            return Colors.TblWltOther
         elif wtype==WalletTypes.Offline:
            return Colors.TblWltOffline
         else:
            return Colors.TblWltMine
      elif role==QtCore.Qt.ForegroundRole:
         if highlighted:
            return Colors.HighlightFG
         elif nConf < 2:
            return Colors.TextNoConfirm
         elif nConf <= 4:
            return Colors.TextSomeConfirm

         if col==COL.Amount:
            toSelf = rowData[COL.toSelf]
            if toSelf:
               return Colors.Mid
            amt = float(rowData[COL.Amount])
            if   amt>0: return Colors.TextGreen
            elif amt<0: return Colors.TextRed
            else:       return Colors.Foreground
      elif role==QtCore.Qt.FontRole:
         if col==COL.Amount:
            f = GETFONT('Fixed')
            f.setWeight(QtGui.QFont.Bold)
            return f
      elif role==QtCore.Qt.ToolTipRole:
         if col in (COL.NumConf, COL.DateStr):
            nConf = rowData[COL.NumConf]
            isCB  = rowData[COL.isCoinbase]
            isConfirmed = (nConf>119 if isCB else nConf>5)
            if isConfirmed:
               return self.tr('Transaction confirmed!\n(%d confirmations)' % nConf)
            else:
               tooltipStr = ''
               if isCB:
                  tooltipStr = self.tr('%d/120 confirmations' & nConf)
                  tooltipStr += ( self.tr('\n\nThis is a "generation" transaction from\n'
                                 'Bitcoin mining.  These transactions take\n'
                                 '120 confirmations (approximately one day)\n'
                                 'before they are available to be spent.'))
               elif flagged:
                  tooltipStr = self.tr("You have create this transaction as RBF (Replaceable By Fee).<br><br>"
                               "This means you have the opportunity to bump the fee on this transaction"
                               " if it fails to confirm quickly enough.<br><br>"
                               "To bump the fee, right click on the ledger entry and pick <u>\"Bump the fee\"</u>")
               elif optInRBF:
                  tooltipStr = self.tr("This transaction has been RBF flagged (Replaceable By Fee)<br><br>"
                               "It means the network will accept and broadcast a double spend of"
                               " the underlying outputs as long as the double spend pays a higher fee.<br><br>"
                               "Do not consider a RBF transaction as a valid payment until it receives"
                               " at least 1 confirmation.")
               elif isChainedZC:
                  tooltipStr = self.tr("This transaction spends ZC (zero confirmation) outputs<br><br>"
                               "Transactions built on top of yet to confirm transactions are at"
                               " risk of being invalidated for as long as the parent transaction"
                               " remains unconfirmed.<br><br>"
                               "It is recommended to wait for at least 1 confirmation before"
                               " accepting these as valid payment.")
               else:
                  tooltipStr = self.tr('%d/6 confirmations' % rowData[COL.NumConf])
                  tooltipStr += self.tr( '\n\nFor small transactions, 2 or 3 '
                                 'confirmations is usually acceptable. '
                                 'For larger transactions, you should '
                                 'wait for 6 confirmations before '
                                 'trusting that the transaction is valid.')
               return str(tooltipStr)
         if col==COL.TxDir:
            #toSelf = self.index(index.row(), COL.toSelf).data().toBool()
            toSelf = rowData[COL.toSelf]
            if toSelf:
               return self.tr('Bitcoins sent and received by the same wallet')
            else:
               txdir = rowData[COL.TxDir]
               if rowData[COL.isCoinbase]:
                  return self.tr('You mined these Bitcoins!')
               if float(txdir.strip())<0:
                  return self.tr('Bitcoins sent')
               else:
                  return self.tr('Bitcoins received')
         if col==COL.Amount:
            if TheSettings.get('DispRmFee'):
               return self.tr('The net effect on the balance of this wallet '
                               '<b>not including transaction fees.</b>  '
                               'You can change this behavior in the Armory '
                               'preferences window.')
            else:
               return self.tr('The net effect on the balance of this wallet, '
                               'including transaction fees.')

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      COL = LEDGERCOLS
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COL.NumConf: return None
            if section==COL.DateStr: return str(self.tr('Date'))
            if section==COL.WltName: return str(self.tr('Lockbox')) if self.isLboxModel else str(self.tr('Wallet'))
            if section==COL.Comment: return str(self.tr('Comments'))
            if section==COL.TxDir:   return None
            if section==COL.Amount:  return str(self.tr('Amount'))
            if section==COL.isOther: return str(self.tr('Other Owner'))
            if section==COL.WltID:   return str(self.tr('Wallet ID'))
            if section==COL.TxHash:  return str(self.tr('Tx Hash (LE)'))
      elif role==QtCore.Qt.TextAlignmentRole:
         return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

   def setLedgerDelegateId(self, delegateId):
      if len(delegateId) == 0:
         raise Exception("empty delegate id")
      self.ledgerDelegateId = delegateId

   def setConvertLedgerMethod(self, method):
      self.convertLedger = method

   def getMoreData(self, atBottom):
      #return 0 if self.ledger didn't change

      if atBottom == True:
         #Try to grab the next page. If it throws, there is no more data
         #so we can simply return
         try:
            newLedger = TheBridge.getHistoryPageForDelegate(\
               self.ledgerDelegateId, self.bottomPage.id +1)
            toTable = self.convertLedger(newLedger)
         except:
            return 0

         self.previousOffset = -len(self.topPage.table)

         #get the length of the ledger we're not dumping
         prevPageCount = len(self.currentPage.table) + \
                         len(self.bottomPage.table)

         #Swap pages downwards
         self.topPage = deepcopy(self.currentPage)
         self.currentPage = deepcopy(self.bottomPage)

         self.bottomPage.id += 1
         self.bottomPage.table = toTable

         #figure out the bottom of the previous view in
         #relation with the new one
         pageCount = prevPageCount + len(self.bottomPage.table)
         if pageCount == 0:
            ratio = 0
         else:
            ratio = float(prevPageCount) / float(pageCount)

      else:
         try:
            newLedger = TheBridge.getHistoryPageForDelegate(\
               self.delegateId, self.topPage.id -1)
            toTable = self.convertLedger(newLedger)
         except:
            return 0

         self.previousOffset = len(self.topPage.table)

         prevPageCount = len(self.currentPage.table) + \
                     len(self.topPage.table)

         self.bottomPage = deepcopy(self.currentPage)
         self.currentPage = deepcopy(self.topPage)

         self.topPage.rawData = newLedger
         self.topPage.id -= 1
         self.topPage.table = toTable

         pageCount = prevPageCount + len(self.topPage.table)
         ratio = 1 - float(prevPageCount) / float(pageCount)

      #call reset, which will pull the missing ledgerTable from C++
      self.reset()
      return ratio

   def reset(self, hard=False):
      #if either top or current page is index 0, update it
      #also if any of the pages has no ledger, pull and convert it
      super(QtCore.QAbstractTableModel, self).beginResetModel()

      if hard == True:
         self.topPage.id = -1
         self.topPage.table = []

         self.currentPage.id = 0
         self.currentPage.table = []

         self.bottomPage.id = 1
         self.bottomPage.table = []

      if self.topPage.id == 0 or not self.topPage.table:
         try:
            self.topPage.rawData = TheBridge.service.getHistoryPageForDelegate(
               self.ledgerDelegateId, self.topPage.id)
            toTable = self.convertLedger(self.topPage.rawData)
            self.topPage.table = toTable
         except:
            pass

      if self.currentPage.id == 0 or not self.currentPage.table:
            self.currentPage.rawData = TheBridge.service.getHistoryPageForDelegate(
               self.ledgerDelegateId, self.currentPage.id)
            toTable = self.convertLedger(self.currentPage.rawData)
            self.currentPage.table = toTable

      if not self.bottomPage.table:
         try:
            self.bottomPage.rawData = TheBridge.service.getHistoryPageForDelegate(
               self.ledgerDelegateId, self.bottomPage.id)
            toTable = self.convertLedger(self.bottomPage.rawData)
            self.bottomPage.table = toTable
         except:
            pass

      self.ledger = []
      self.ledger.extend(self.topPage.table)
      self.ledger.extend(self.currentPage.table)
      self.ledger.extend(self.bottomPage.table)

      super(QtCore.QAbstractTableModel, self).endResetModel()

   def centerAtHeight(self, blk):
      #return the index for that block height in the new ledger
      centerId = self.ledgerDelegate.getPageIdForBlockHeight(blk)

      self.bottomPage = TableEntry(centerId +1, [])
      self.currentPage = TableEntry(centerId, [])
      self.topPage = TableEntry(centerId -1, [])

      self.reset()

      blockDiff = 2**32
      blockReturn = 0

      for leID in range(0, len(self.ledger)):
         block = TheBDM.getTopBlockHeight() - self.ledger[leID][0] -1
         diff = abs(block - blk)
         if blockDiff >= diff :
            blockDiff = diff
            blockReturn = leID

      return blockReturn

   def updateIndexComment(self, index, comment):
      #this allows the code to update comments without the need
      #to reload the entire model

      row = index.row()
      rowData = self.ledger[row]
      rowData[LEDGERCOLS.Comment] = comment

   def getRawDataEntry(self, filter):
      def filterRawData(rawDataList, filter):
         for data in rawDataList:
            for ledger in data.ledgers:
               if filter(ledger):
                  return ledger
         return None

      if self.bottomPage.rawData:
         result = filterRawData(self.bottomPage.rawData, filter)
         if result:
            return result

      if self.currentPage.rawData:
         result = filterRawData(self.currentPage.rawData, filter)
         if result:
            return result

      if self.topPage.rawData:
         result = filterRawData(self.topPage.rawData, filter)
         if result:
            return result

      return None

################################################################################
class CalendarDialog(ArmoryDialog):
   def __init__(self, parent, main):
      super(CalendarDialog, self).__init__(parent, main)

      self.parent = parent
      self.main = main

      self.calendarWidget = QCalendarWidget(self)
      self.layout = QtWidgets.QGridLayout()
      self.layout.addWidget(self.calendarWidget, 0, 0)
      self.setLayout(self.layout)

      self.adjustSize()

      self.calendarWidget.selectionChanged.connect(self.accept)

################################################################################
class ArmoryBlockAndDateSelector(QtCore.QObject):
   hideIt = QtCore.Signal()

   def __init__(self, parent, main, controlFrame):
      super(ArmoryBlockAndDateSelector, self).__init__()

      self.parent = parent
      self.main = main

      self.ledgerDelegate = None
      self.Height = 0
      self.Width  = 0

      self.Block = 0
      self.Date = 0

      self.isExpanded = False
      self.doHide = False
      self.isEditingBlockHeight = False

      self.frmBlockAndDate = QtWidgets.QFrame()
      self.frmBlockAndDateLayout = QtWidgets.QGridLayout()
      self.frmBlockAndDateLayout.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignBottom)

      self.lblBlock = QtWidgets.QLabel(self.main.tr("<a href=edtBlock>Block:</a>"))
      self.lblBlock.linkActivated.connect(self.linkClicked)
      self.lblBlock.adjustSize()
      self.lblBlockValue = QtWidgets.QLabel("")
      self.lblBlockValue.adjustSize()

      self.lblDate = QtWidgets.QLabel(self.main.tr("<a href=edtDate>Date:</a>"))
      self.lblDate.linkActivated.connect(self.linkClicked)
      self.lblDate.adjustSize()
      self.lblDateValue = QtWidgets.QLabel("")
      self.lblDateValue.adjustSize()

      self.lblTop = QtWidgets.QLabel(self.main.tr("<a href=goToTop>Top</a>"))
      self.lblTop.linkActivated.connect(self.goToTop)
      self.lblTop.adjustSize()

      self.calendarDlg = CalendarDialog(self.parent, self.main)

      self.edtBlock = QtWidgets.QLineEdit()
      edtFontMetrics = self.edtBlock.fontMetrics()
      fontRect = edtFontMetrics.boundingRect("00000000")
      self.edtBlock.setFixedWidth(fontRect.width())
      self.edtBlock.setVisible(False)
      self.edtBlock.editingFinished.connect(self.blkEditingFinished)

      self.frmBlock = QtWidgets.QFrame()
      self.frmBlockLayout = QtWidgets.QGridLayout()
      self.frmBlockLayout.addWidget(self.lblBlock, 0, 0)
      self.frmBlockLayout.addWidget(self.lblBlockValue, 0, 1)
      self.frmBlockLayout.addWidget(self.edtBlock, 0, 1)
      self.frmBlockLayout.addWidget(self.lblTop, 0, 2)
      self.frmBlock.setLayout(self.frmBlockLayout)
      self.frmBlock.adjustSize()

      self.frmBlockAndDateLayout.addWidget(self.lblBlock, 0, 0)
      self.frmBlockAndDateLayout.addWidget(self.lblBlockValue, 0, 1)
      self.frmBlockAndDateLayout.addWidget(self.edtBlock, 0, 1)
      self.frmBlockAndDateLayout.addWidget(self.lblTop, 0, 2)

      self.frmBlockAndDateLayout.addWidget(self.lblDate, 1, 0)
      self.frmBlockAndDateLayout.addWidget(self.lblDateValue, 1, 1)

      self.frmBlockAndDate.setLayout(self.frmBlockAndDateLayout)
      self.frmBlockAndDate.setBackgroundRole(QtGui.QPalette.Window)
      self.frmBlockAndDate.setAutoFillBackground(True)
      self.frmBlockAndDate.setFrameStyle(QtWidgets.QFrame.Panel | QtWidgets.QFrame.Raised)
      self.frmBlockAndDate.setVisible(False)
      self.frmBlockAndDate.setMouseTracking(True)
      self.frmBlockAndDate.leaveEvent = self.triggerHideBlockAndDate
      self.frmBlockAndDate.enterEvent = self.resetHideBlockAndDate

      self.dateBlockSelectButton = QtWidgets.QPushButton('Goto')
      self.dateBlockSelectButton.setStyleSheet('QtWidgets.QPushButton { font-size : 10px }')
      self.dateBlockSelectButton.setMaximumSize(60, 20)
      self.dateBlockSelectButton.clicked.connect(self.showBlockDateController)


      self.frmLayout = QtWidgets.QGridLayout()
      self.frmLayout.addWidget(self.dateBlockSelectButton)
      self.frmLayout.addWidget(self.frmBlockAndDate)
      self.frmLayout.setAlignment(QtCore.Qt.AlignCenter | QtCore.Qt.AlignTop)
      self.frmLayout.setMargin(0)

      self.hideIt.connect(self.hideBlockAndDate)
      self.dateBlockSelectButton.setVisible(True)

      controlFrame.setLayout(self.frmLayout)

   def linkClicked(self, link):
      if link == 'edtBlock':
         self.editBlockHeight()
      elif link == 'edtDate':
         self.editDate()

   def updateLabel(self, block):
      self.Block = block

      try:
         self.Date = TheBridge.getBlockTimeByHeight(block)
         datefmt = self.main.getPreferredDateFormat()
         dateStr = unixTimeToFormatStr(self.Date, datefmt)
      except:
         dateStr = "N/A"

      self.lblBlockValue.setText(str(block)  )
      self.lblBlockValue.adjustSize()
      self.lblDateValue.setText(dateStr)
      self.lblDateValue.adjustSize()

      self.frmBlockAndDate.adjustSize()

      if self.isExpanded == True:
         fontRect = self.frmBlockAndDate.geometry()
      else:
         fontRect = self.dateBlockSelectButton.geometry()

      self.Width = fontRect.width()
      self.Height = fontRect.height()

   def getLayoutSize(self):
      return QtCore.QSize(self.Width, self.Height)

   def pressEvent(self, mEvent):
      if mEvent.button() == QtCore.Qt.LeftButton:
         self.lblClicked()

   def showBlockDateController(self):
      self.isExpanded = True
      self.dateBlockSelectButton.setVisible(False)
      self.frmBlockAndDate.setVisible(True)

      self.updateLabel(self.Block)

   def prepareToHideThread(self):
      self.doHide = True
      time.sleep(1)

      self.hiteIt.emit()

   def triggerHideBlockAndDate(self, mEvent):
      hideThread = PyBackgroundThread(self.prepareToHideThread)
      hideThread.start()

   def hideBlockAndDate(self):
      if self.isExpanded == True and self.doHide == True:
         self.frmBlockAndDate.setVisible(False)
         self.dateBlockSelectButton.setVisible(True)
         self.isExpanded = False

         self.updateLabel(self.Block)

   def resetHideBlockAndDate(self, mEvent):
      self.doHide = False

   def editBlockHeight(self):
      if self.isEditingBlockHeight == False:
         self.edtBlock.setText(self.lblBlockValue.text())
         self.lblBlockValue.setVisible(False)
         self.edtBlock.setVisible(True)
         self.isEditingBlockHeight = True
         self.frmBlockAndDate.adjustSize()
      else:
         self.lblBlockValue.setVisible(True)
         self.edtBlock.setVisible(False)
         self.isEditingBlockHeight = False
         self.frmBlockAndDate.adjustSize()

   def editDate(self):
      if self.isEditingBlockHeight == True:
         self.editBlockHeight()

      if self.calendarDlg.exec_() == True:
         self.dateChanged()

   def blkEditingFinished(self):
      try:
         blk = int(self.edtBlock.text())
         self.Block = self.ledgerDelegate.getBlockInVicinity(blk)
         self.Date = TheBDM.bdv().getBlockTimeByHeight(self.Block)
      except:
         pass

      self.editBlockHeight()
      self.updateLabel(self.Block)

      self.parent.emit(SIGNAL('centerView'), self.Block)

   def dateChanged(self):
      try:
         ddate = self.calendarDlg.calendarWidget.selectedDate().toPyDate()
         self.Date = int(time.mktime(ddate.timetuple()))

         self.Block = TheBDM.bdv().getClosestBlockHeightForTime(self.Date)
      except:
         pass

      self.updateLabel(self.Block)
      self.parent.emit(SIGNAL('centerView'), self.Block)

   def goToTop(self):
      if self.isEditingBlockHeight == True:
         self.editBlockHeight()
      self.parent.emit(SIGNAL('goToTop'))

   def hide(self):
      self.dateBlockSelectButton.setVisible(False)

   def show(self):
      if self.isExpanded == False:
         self.dateBlockSelectButton.setVisible(True)


################################################################################
class ArmoryTableView(QtWidgets.QTableView):
   #centerViewSignal = QtCore.Signal()
   #goToTopSignal = QtCore.Signal()

   def __init__(self, parent, main, controlFrame):
      super(ArmoryTableView, self).__init__(parent)

      self.parent = parent
      self.main = main

      self.BlockAndDateSelector = ArmoryBlockAndDateSelector(self, self.main, controlFrame)
      self.verticalScrollBar().rangeChanged.connect(self.scrollBarRangeChanged)
      self.vBarRatio = 0
      self.verticalScrollBar().setVisible(False)
      self.setSelectionMode(QAbstractItemView.SingleSelection)

      self.prevIndex = -1

      #self.centerViewSignal.connect(self.centerViewAtBlock)
      #self.goToTopSignal.connect(self.goToTop)

   def verticalScrollbarValueChanged(self, dx):

      if dx > self.verticalScrollBar().maximum() - PAGE_LOAD_OFFSET:
         #at the bottom of the scroll area
         ratio = self.ledgerModel.getMoreData(True)
         if ratio != 0:
            self.vBarRatio = ratio

      elif dx < PAGE_LOAD_OFFSET:
         #at the top of the scroll area
         ratio = self.ledgerModel.getMoreData(False)
         if ratio != 0:
            self.vBarRatio = ratio

      self.updateBlockAndDateLabel()

   def setModel(self, model):
      super(ArmoryTableView, self).setModel(model)
      self.ledgerModel = model
      if model is not None:
         self.BlockAndDateSelector.ledgerDelegate = self.ledgerModel.ledgerDelegate

   def scrollBarRangeChanged(self, rangeMin, rangeMax):
      pos = int(self.vBarRatio * rangeMax)
      self.verticalScrollBar().setValue(pos)
      self.updateBlockAndDateLabel()

   def selectionChanged(self, itemSelected, itemDeselected):
      if itemSelected.last().bottom() +2 >= len(self.ledgerModel.ledger):
         ratio = self.ledgerModel.getMoreData(True)
         if ratio != 0:
            self.vBarRatio = ratio
      elif itemSelected.last().top() -2 <= 0:
         ratio = self.ledgerModel.getMoreData(False)
         if ratio != 0:
            self.vBarRatio = ratio

      super(ArmoryTableView, self).selectionChanged(itemSelected, itemDeselected)
      self.updateBlockAndDateLabel()

   def moveCursor(self, action, modifier):
      self.prevIndex = self.currentIndex().row()
      if action == QAbstractItemView.MoveUp:
         if self.currentIndex().row() > 0:
            return self.ledgerModel.index(self.currentIndex().row() -1, 0)

      elif action == QAbstractItemView.MoveDown:
         if self.currentIndex().row() < self.ledgerModel.rowCount() -1:
            return self.ledgerModel.index(self.currentIndex().row() +1, 0)

      return self.currentIndex()

   def reset(self):
      #save the previous selection
      super(ArmoryTableView, self).reset()

      if self.prevIndex != -1:
         self.setCurrentIndex(\
            self.ledgerModel.index(self.ledgerModel.previousOffset + self.prevIndex, 0))

      self.updateBlockAndDateLabel()

   def centerViewAtBlock(self, Blk):
      itemIndex = self.ledgerModel.centerAtHeight(Blk)
      self.vBarRatio = float(itemIndex) / float(self.ledgerModel.rowCount())
      self.verticalScrollBar().setValue(\
         self.vBarRatio * self.verticalScrollBar().maximum())

   def updateBlockAndDateLabel(self):
      try:
         sbMax = self.verticalScrollBar().maximum()
         if sbMax == 0:
            self.BlockAndDateSelector.hide()
         else:
            self.BlockAndDateSelector.show()

         ratio = float(self.verticalScrollBar().value()) \
              / float(self.verticalScrollBar().maximum())

         leID = int(ratio * float(self.ledgerModel.rowCount()))
         block = TheBDM.getTopBlockHeight() - self.ledgerModel.ledger[leID][0] +1
         self.BlockAndDateSelector.updateLabel(block)
      except:
         pass

   def goToTop(self):
      self.ledgerModel.reset(True)
      self.vBarRatio = 0
      self.verticalScrollBar().setValue(0)

################################################################################
class LedgerDispSortProxy(QtCore.QSortFilterProxyModel):
   """
   Acts as a proxy that re-maps indices to the table view so that data
   appears sorted, without actually touching the model
   """
   def lessThan(self, idxLeft, idxRight):
      COL = LEDGERCOLS
      thisCol  = self.sortColumn()

      def getDouble(idx, col):
         return float(self.sourceModel().ledger[idx.row()][col])

      def getInt(idx, col):
         return int(self.sourceModel().ledger[idx.row()][col])


      #LEDGERCOLS  = enum('NumConf', 'UnixTime', 'DateStr', 'TxDir', 'WltName', 'Comment', \
                        #'Amount', 'isOther', 'WltID', 'TxHash', 'toSelf', 'DoubleSpend')
      if thisCol==COL.NumConf:
         lConf = getInt(idxLeft,  COL.NumConf)
         rConf = getInt(idxRight, COL.NumConf)
         if lConf==rConf:
            tLeft  = getDouble(idxLeft,  COL.UnixTime)
            tRight = getDouble(idxRight, COL.UnixTime)
            return (tLeft<tRight)
         return (lConf>rConf)
      if thisCol==COL.DateStr:
         tLeft  = getDouble(idxLeft,  COL.UnixTime)
         tRight = getDouble(idxRight, COL.UnixTime)
         return (tLeft<tRight)
      if thisCol==COL.Amount:
         btcLeft  = getDouble(idxLeft,  COL.Amount)
         btcRight = getDouble(idxRight, COL.Amount)
         return (abs(btcLeft) < abs(btcRight))
      else:
         return super(LedgerDispSortProxy, self).lessThan(idxLeft, idxRight)


################################################################################
class LedgerDispDelegate(QtWidgets.QStyledItemDelegate):

   COL = LEDGERCOLS

   def __init__(self, parent=None):
      super(LedgerDispDelegate, self).__init__(parent)


   def paint(self, painter, option, index):
      bgcolor = QtGui.QPalette().color(
         QtGui.QPalette.Active, QtGui.QPalette.Window)
      if option.state & QtWidgets.QStyle.State_Selected:
         bgcolor = QtWidgets.QApplication.palette().highlight().color()

      isCoinbase = False
      if index.model().index(index.row(), self.COL.isCoinbase).data() == "True":
         isCoinbase = True

      toSelf = False
      if index.model().index(index.row(), self.COL.toSelf).data() == "True":
         toSelf = True

      if index.column() == self.COL.NumConf:
         nConf = int(index.model().data(index))

         image=None
         if isCoinbase:
            if nConf<120:
               effectiveNConf = int(6*float(nConf)/120.)
               image = QtGui.QImage('./img/conf%dt_nonum.png'%effectiveNConf)
            else:
               image = QtGui.QImage('./img/conf6t.png')
         else:
            if nConf<6:
               image = QtGui.QImage('./img/conf%dt.png'%nConf)
            else:
               image = QtGui.QImage('./img/conf6t.png')
         painter.fillRect(option.rect, bgcolor)
         pixmap = QtGui.QPixmap.fromImage(image)
         #pixmap.scaled(70, 30, QtCore.Qt.KeepAspectRatio)
         painter.drawPixmap(option.rect, pixmap)
      elif index.column() == self.COL.TxDir:
         # This is frustrating... QVariant doesn't support 64-bit ints
         # So I have to pass the amt as string, then convert here to long
         image = QtGui.QImage()

         # isCoinbase still needs to be flagged in the C++ utils
         if isCoinbase:
            image = QtGui.QImage('./img/moneyCoinbase.png')
         elif toSelf:
            image = QtGui.QImage('./img/moneySelf.png')
         else:
            txdir = str(index.model().data(index)).strip()
            if txdir[0].startswith('-'):
               image = QtGui.QImage('./img/moneyOut.png')
            else:
               image = QtGui.QImage('./img/moneyIn.png')

         painter.fillRect(option.rect, bgcolor)
         pixmap = QtGui.QPixmap.fromImage(image)
         #pixmap.scaled(70, 30, QtCore.Qt.KeepAspectRatio)
         painter.drawPixmap(option.rect, pixmap)
      else:
         QtWidgets.QStyledItemDelegate.paint(self, painter, option, index)

   def sizeHint(self, option, index):
      if index.column()==self.COL.NumConf:
         return QtCore.QSize(28,28)
      elif index.column()==self.COL.TxDir:
         return QtCore.QSize(28,28)
      return QtWidgets.QStyledItemDelegate.sizeHint(self, option, index)


################################################################################
class WalletAddrDispModel(QtCore.QAbstractTableModel):

   def __init__(self, wlt, mainWindow):
      super(WalletAddrDispModel, self).__init__()
      self.main = mainWindow
      self.wlt = wlt

      self.noChange = False
      self.usedOnly = False
      self.notEmpty = False

      self.filterAddrList()


   def setFilter(self, filt1=False, filt2=False, filt3=False):
      self.notEmpty = filt1
      self.noChange = filt2
      self.usedOnly = filt3
      self.filterAddrList()


   def filterAddrList(self):
      addrList = self.wlt.getLinearAddrList()

      if self.notEmpty and TheBDM.getState()==BDM_BLOCKCHAIN_READY:
         hasBalance = lambda a: (self.wlt.getAddrBalance(a.getAddr160(), 'Full')>0)
         addrList = filter(hasBalance, addrList)

      if self.noChange:
         notChange = lambda a: (self.wlt.getCommentForAddress(a.getAddr160()) != CHANGE_ADDR_DESCR_STRING)
         addrList = filter(notChange, addrList)

      if self.usedOnly and TheBDM.getState()==BDM_BLOCKCHAIN_READY:
         isUsed = lambda a: (self.wlt.getAddrTotalTxnCount(Hash160ToScrAddr(a.getAddr160())))
         addrList = filter(isUsed, addrList)

      self.addr160List = [a.getPrefixedAddr() for a in addrList]


   @TimeThisFunction
   def reset(self):
      self.filterAddrList()
      super(WalletAddrDispModel, self).reset()



   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.addr160List)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 5

   def data(self, index, role=QtCore.Qt.DisplayRole):
      COL = ADDRESSCOLS
      row,col = index.row(), index.column()
      if row>=len(self.addr160List):
         return None
      addr = self.wlt.addrMap[self.addr160List[row]]
      addr_index = addr.chainIndex

      addrHash = addr.getPrefixedAddr()
      addrB58 = addr.getAddressString()
      chainIdx = addr.chainIndex+1  # user must get 1-indexed
      if role==QtCore.Qt.DisplayRole:
         if col==COL.Address:
            return addrB58
         if col==COL.Comment:
            return self.wlt.getComment(addrHash)
         if col==COL.NumTx:
            if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
               return 'n/a'
            return addr.getTxioCount()
         if col==COL.ChainIdx:
            if self.wlt.addrMap[addrHash].chainIndex==-2:
               return 'Imported'
            else:
               return chainIdx
         if col==COL.Balance:
            if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
               return '(...)'
            addrFullBalance = self.wlt.getAddrBalance(addrHash, balType="full")
            return coin2str(addrFullBalance, maxZeros=2)
      elif role==QtCore.Qt.TextAlignmentRole:
         if col in (COL.Address, COL.Comment, COL.ChainIdx):
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         elif col in (COL.NumTx,):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         elif col in (COL.Balance,):
            if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
               return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
            else:
               return int(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.ForegroundRole:
         if col==COL.Balance:
            if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
               return Colors.Foreground
            addrFullBalance = self.wlt.getAddrBalance(addrHash, balType="full")
            if addrFullBalance>0: return Colors.TextGreen
            else: return Colors.Foreground
      elif role==QtCore.Qt.FontRole:
         try:
            hasTx = addr.getTxioCount()>0
         except:
            hasTx = False

         cmt = self.index(index.row(),COL.Comment).data()
         isChange = (cmt==CHANGE_ADDR_DESCR_STRING)

         if col==COL.Balance:
            return GETFONT('Fixed',bold=hasTx)
         if col==COL.ChainIdx:
            return GETFONT('Var')
         else:
            doBold   = hasTx and not isChange
            doItalic = isChange
            return GETFONT('Var',bold=doBold, italic=doItalic)
      elif role==QtCore.Qt.ToolTipRole:
         if col==COL.ChainIdx:
            cmt = self.index(index.row(),COL.ChainIdx).data()
            if cmt.strip().lower().startswith('imp'):
               return self.tr('<u></u>This is an imported address. Imported '
                               'addresses are not protected by regular paper '
                               'backups.  You must use the "Backup Individual '
                               'Keys" option to protect it.')
            else:
               return self.tr('<u></u>The order that this address was '
                               'generated in this wallet')
         cmt = self.index(index.row(),COL.Comment).data()
         if cmt==CHANGE_ADDR_DESCR_STRING:
            return self.tr('This address was created by Armory to '
                            'receive change-back-to-self from an oversized '
                            'transaction.')
      elif role==QtCore.Qt.BackgroundColorRole:
         if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
            return Colors.TblWltOther

         addrFullBalance = self.wlt.getAddrBalance(addrHash, balType="full")
         if addrFullBalance>0:
            return Colors.SlightGreen
         else:
            return Colors.TblWltOther

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      COL = ADDRESSCOLS
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COL.ChainIdx: return '#'
            if section==COL.Address:  return self.tr('Address')
            if section==COL.Comment:  return self.tr('Comment')
            if section==COL.NumTx:    return self.tr('#Tx')
            if section==COL.Balance:  return self.tr('Balance')
         elif role==QtCore.Qt.TextAlignmentRole:
            if section in (COL.Address, COL.Comment):
               return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            elif section in (COL.NumTx,):
               return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
            elif section in (COL.Balance,):
               return int(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)

      return None


################################################################################
class WalletAddrSortProxy(QtCore.QSortFilterProxyModel):
   """
   Acts as a proxy that re-maps indices to the table view so that data
   appears sorted, without actually touching the model
   """
   def lessThan(self, idxLeft, idxRight):
      COL = ADDRESSCOLS
      thisCol  = self.sortColumn()
      strLeft  = self.sourceModel().data(idxLeft)
      strRight = self.sourceModel().data(idxRight)
      if thisCol==COL.Address:
         return (strLeft.lower() < strRight.lower())
      elif thisCol==COL.Comment:
         return (strLeft < strRight)
      elif thisCol==COL.Balance:
         return (float(strLeft.strip()) < float(strRight.strip()))
      elif thisCol==COL.ChainIdx:
         left = -2 if strLeft =='Imported' else int(strLeft)
         rght = -2 if strRight=='Imported' else int(strRight)
         return (left<rght)
      else:
         return super(WalletAddrSortProxy, self).lessThan(idxLeft, idxRight)


################################################################################
class TxInDispModel(QtCore.QAbstractTableModel):
   def __init__(self,  pytx, txinListFromBDM=None, main=None):
      super(TxInDispModel, self).__init__()
      self.main = main
      self.txInList = txinListFromBDM[:]
      self.dispTable = []

      # If this is actually a USTX in here, then let's use that
      # We do this to make sure we have somewhere to put USTX-specific
      # code, but we don't really need it yet, except to identify
      # signed/unsigned in the table
      ustx = None
      if isinstance(pytx, UnsignedTransaction):
         ustx = pytx
         pytx = ustx.pytxObj
      self.tx = pytx.copy()

      for i,txin in enumerate(self.tx.inputs):
         self.dispTable.append([])
         wltID = ''
         scrType = getTxInScriptType(txin)
         if txinListFromBDM and len(txinListFromBDM[i][0])>0:
            # We had a BDM to help us get info on each input -- use it
            scrAddr,val,blk,hsh,idx,script = txinListFromBDM[i]
            dispInfo = self.main.getDisplayStringForScript(script, 60)
            addrStr = dispInfo['String']
            wltID   = dispInfo['WltID']
            if not wltID:
               wltID  = dispInfo['LboxID']
            if not wltID:
               wltID = ''

            dispcoin  = '' if not val else coin2str(val,maxZeros=1)
            self.dispTable[-1].append(wltID)
            self.dispTable[-1].append(addrStr)
            self.dispTable[-1].append(dispcoin)
            self.dispTable[-1].append(binary_to_hex(hsh))
            self.dispTable[-1].append(idx)
            self.dispTable[-1].append(blk)
            if len(script) == 0:
               self.dispTable[-1].append("")
            elif ustx is None:
               self.dispTable[-1].append(CPP_TXIN_SCRIPT_NAMES[scrType])
            else:
               isSigned = ustx.ustxInputs[i].evaluateSigningStatus(ustx.signer).allSigned
               self.dispTable[-1].append('Signed' if isSigned else 'Unsigned')

            self.dispTable[-1].append(int_to_hex(txin.intSeq, widthBytes=4))
            self.dispTable[-1].append(binary_to_hex(txin.binScript))
         else:
            # We don't have any info from the BDM, display whatever we can
            # (which usually isn't much)
            recipAddr = '<Unknown>'
            recipAddr = TxInExtractAddrStrIfAvail(txin)
            atype, a160 = '',''
            if len(recipAddr) > 0:
               atype, a160 = addrStr_to_hash160(recipAddr)
               wltID = self.main.getWalletForAddrHash(a160)

            self.dispTable[-1].append(wltID)
            self.dispTable[-1].append(a160)
            self.dispTable[-1].append('<Unknown>')
            self.dispTable[-1].append(binary_to_hex(txin.outpoint.txHash))
            self.dispTable[-1].append(str(txin.outpoint.txOutIndex))
            self.dispTable[-1].append('')
            self.dispTable[-1].append(CPP_TXIN_SCRIPT_NAMES[scrType])
            self.dispTable[-1].append(int_to_hex(txin.intSeq, widthBytes=4))
            self.dispTable[-1].append(binary_to_hex(txin.binScript))



   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.dispTable)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 10

   #TXINCOLS  = enum('WltID', 'Sender', 'Btc', 'OutPt', 'OutIdx', 'FromBlk', 'ScrType', 'Sequence', 'Script')
   def data(self, index, role=QtCore.Qt.DisplayRole):
      COLS = TXINCOLS
      row,col = index.row(), index.column()
      try:
         wlt = self.main.wallets.getByIndex(row)
      except:
         wlt = None

      wltID = self.dispTable[row][COLS.WltID]
      if role==QtCore.Qt.DisplayRole:
         return self.dispTable[row][col]
      elif role==QtCore.Qt.TextAlignmentRole:
         if col in (COLS.WltID, COLS.Sender, COLS.OutPt):
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         elif col in (COLS.OutIdx, COLS.FromBlk, COLS.Sequence, COLS.ScrType):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         elif col in (COLS.Btc,):
            return int(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.BackgroundColorRole:
         if self.dispTable[row][COLS.WltID] and wlt:
            wtype = determineWalletType(wlt, self.main)[0]
            if wtype==WalletTypes.WatchOnly:
               return Colors.TblWltOther
            elif wtype==WalletTypes.Offline:
               return Colors.TblWltOffline
            else:
               return Colors.TblWltMine
      elif role==QtCore.Qt.FontRole:
         if col==COLS.Btc:
            return GETFONT('Fixed')

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      COLS = TXINCOLS
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COLS.WltID:    return self.tr('Wallet ID')
            if section==COLS.Sender:   return self.tr('Sender')
            if section==COLS.Btc:      return self.tr('Amount')
            if section==COLS.OutPt:    return self.tr('Prev. Tx Hash')
            if section==COLS.OutIdx:   return self.tr('Index')
            if section==COLS.FromBlk:  return self.tr('From Block#')
            if section==COLS.ScrType:  return self.tr('Script Type')
            if section==COLS.Sequence: return self.tr('Sequence')
            if section==COLS.Script:   return self.tr('Script')
      elif role==QtCore.Qt.TextAlignmentRole:
         if orientation==QtCore.Qt.Horizontal:
            if section in (COLS.WltID, COLS.Sender, COLS.OutPt):
               return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            elif section in (COLS.OutIdx, COLS.FromBlk, COLS.Btc, COLS.Sequence, COLS.ScrType):
               return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)






################################################################################
class TxOutDispModel(QtCore.QAbstractTableModel):
   def __init__(self,  pytx, main=None, idxGray=[]):
      super(TxOutDispModel, self).__init__()
      self.tx = pytx.copy()

      self.main = main
      self.txOutList = []
      self.wltIDList = []
      self.idxGray = idxGray[:]
      for i,txout in enumerate(self.tx.outputs):
         self.txOutList.append(txout)

   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.txOutList)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 6

   #TXOUTCOLS = enum('WltID', 'Recip', 'Btc', 'ScrType')
   def data(self, index, role=QtCore.Qt.DisplayRole):
      COLS = TXOUTCOLS
      row,col = index.row(), index.column()
      txout = self.txOutList[row]
      dispInfo = self.main.getDisplayStringForScript(txout.binScript, 60)
      wltID = ''
      if dispInfo['WltID']:
         wltID = dispInfo['WltID']
      elif dispInfo['LboxID']:
         wltID = dispInfo['LboxID']

      stype = dispInfo['ScrType']
      stypeStr = CPP_TXOUT_SCRIPT_NAMES[stype]
      if stype==CPP_TXOUT_MULTISIG:
         M,N = getMultisigScriptInfo(txout.binScript)[:2]
         stypeStr = 'MultiSig[%d-of-%d]' % (M,N)

      if role==QtCore.Qt.DisplayRole:
         if col==COLS.WltID:   return str(wltID)
         if col==COLS.ScrType: return str(stypeStr)
         if col==COLS.Script:  return str(binary_to_hex(txout.binScript))
         if col==COLS.Btc:     return str(coin2str(txout.getValue(),maxZeros=2))
         if col==COLS.Recip:   return str(dispInfo['String'])
         if col==COLS.AddrStr: return str(dispInfo['AddrStr'])
      elif role==QtCore.Qt.TextAlignmentRole:
         if col==COLS.Recip:   return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         if col==COLS.Btc:     return int(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
         if col==COLS.ScrType: return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.ForegroundRole:
         if row in self.idxGray:
            return Colors.Mid
      elif role==QtCore.Qt.BackgroundColorRole:
         if dispInfo['dbId']:
            wlt = self.main.wallets.get(dispInfo['dbId'])
            wtype = determineWalletType(wlt, self.main)[0]
            if wtype==WalletTypes.WatchOnly:
               return Colors.TblWltOther
            if wtype==WalletTypes.Offline:
               return Colors.TblWltOffline
            else:
               return Colors.TblWltMine
      elif role==QtCore.Qt.FontRole:
         if col==COLS.Btc:
            return GETFONT('Fixed')

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      COLS = TXOUTCOLS
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COLS.WltID:   return self.tr('Wallet ID')
            if section==COLS.Recip:   return self.tr('Recipient')
            if section==COLS.Btc:     return self.tr('Amount')
            if section==COLS.ScrType: return self.tr('Script Type')
      elif role==QtCore.Qt.TextAlignmentRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COLS.WltID:   return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            if section==COLS.Recip:   return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            if section==COLS.Btc:     return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
            if section==COLS.ScrType: return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

################################################################################
class SentToAddrBookModel(QtCore.QAbstractTableModel):
   def __init__(self, wlt, main):
      super(SentToAddrBookModel, self).__init__()
      self.main  = main
      self.wlt   = wlt
      self.addrBook = []
      addressBook = self.wlt.createAddressBook()
      for abe in addressBook.entries:
         scrAddr = abe.scrAddr
         try:
            # Only grab addresses that are not in any of your Armory wallets
            if not self.main.wallets.getWltForScrAddr(scrAddr):
               txHashList = abe.txHashes
               self.addrBook.append( [scrAddr, txHashList] )
         except Exception as e:
            # This is not necessarily an error. It could be a lock box LOGERROR(str(e))
            pass

   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.addrBook)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 4

   def data(self, index, role=QtCore.Qt.DisplayRole):
      COL = ADDRBOOKCOLS
      row,col  = index.row(), index.column()
      scrAddr  = self.addrBook[row][0]
      if scrAddr[0] in [ADDRBYTE, P2SHBYTE]:
         addrB58 = scrAddr_to_addrStr(scrAddr)
         addr160 = scrAddr[1:]
      else:
         addrB58 = ''
         addr160 = ''
      wlt      = self.main.wallets.getWltForScrAddr(scrAddr)
      wltID    = wlt.walletId
      txList   = self.addrBook[row][1]
      numSent  = len(txList)
      comment  = self.wlt.getCommentForTxList(scrAddr, txList)

      #ADDRBOOKCOLS = enum('Address', 'WltID', 'NumSent', 'Comment')
      if role==QtCore.Qt.DisplayRole:
         if col==COL.Address:
            return addrB58
         if col==COL.NumSent:
            return numSent
         if col==COL.Comment:
            return comment
         if col==COL.WltID:
            return wltID
      elif role==QtCore.Qt.TextAlignmentRole:
         if col in (COL.Address, COL.Comment, COL.WltID):
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         elif col in (COL.NumSent,):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.FontRole:
         isFreqAddr = (numSent>1)
         return GETFONT('Var', bold=isFreqAddr)
      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      COL = ADDRBOOKCOLS
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COL.Address:  return self.tr('Address')
            if section==COL.WltID:    return self.tr('Ownership')
            if section==COL.NumSent:  return self.tr('Times Used')
            if section==COL.Comment:  return self.tr('Comment')
      elif role==QtCore.Qt.TextAlignmentRole:
         if section in (COL.Address, COL.Comment, COL.WltID):
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         elif section in (COL.NumSent,):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      return None

   def reset(self):
      self.beginResetModel()
      self.endResetModel()

################################################################################
class SentAddrSortProxy(QtCore.QSortFilterProxyModel):
   def lessThan(self, idxLeft, idxRight):
      COL = ADDRBOOKCOLS
      thisCol  = self.sortColumn()
      strLeft  = self.sourceModel().data(idxLeft)
      strRight = self.sourceModel().data(idxRight)

      if thisCol==COL.Address:
         return (strLeft.lower() < strRight.lower())
      else:
         return super(SentAddrSortProxy, self).lessThan(idxLeft, idxRight)

################################################################################
class PromissoryCollectModel(QtCore.QAbstractTableModel):
   def __init__(self, main, promNoteList):
      super(PromissoryCollectModel, self).__init__()
      self.main = main
      self.promNoteList = promNoteList

   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.promNoteList)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 4

   def data(self, index, role=QtCore.Qt.DisplayRole):
      COL = PROMCOLS
      row,col = index.row(), index.column()
      prom = self.promNoteList[row]
      if role==QtCore.Qt.DisplayRole:
         if col==COL.PromID:
            return prom.promID
         elif col==COL.Label:
            return prom.promLabel
         elif col==COL.PayAmt:
            return coin2str(prom.dtxoTarget.value, maxZeros=2)
         elif col==COL.FeeAmt:
            return coin2str(prom.feeAmt, maxZeros=2)
      elif role==QtCore.Qt.TextAlignmentRole:
         if col in [COL.PromID, COL.Label]:
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
         else:
            return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
      elif role==QtCore.Qt.ForegroundRole:
         if col == COL.PayAmt:
            if prom.dtxoTarget.value>0:
               return Colors.TextGreen
         elif col == COL.FeeAmt:
            if prom.feeAmt>0:
               return Colors.TextGreen
      elif role==QtCore.Qt.FontRole:
         if col in [COL.PayAmt, COL.FeeAmt]:
            return GETFONT('Fixed')

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      colLabels = [self.tr('Note ID'), self.tr('Label'), self.tr('Funding'), self.tr('Fee')]
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            return colLabels[section]
      elif role==QtCore.Qt.TextAlignmentRole:
         return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
