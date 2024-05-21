################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2024, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
from os import path
import platform
import sys

from armoryengine.ArmoryUtils import enum, DEFAULT_DATE_FORMAT

from qtpy import QtCore

LOCKBOXCOLS = enum('ID', 'MSType', 'CreateDate', 'LBName', \
                   'Key0', 'Key1', 'Key2', 'Key3', 'Key4', \
                   'NumTx', 'Balance', 'UnixTime')


class LockboxDisplayModel(QtCore.QAbstractTableModel):

   def __init__(self, main, allLockboxes, dateFormat=DEFAULT_DATE_FORMAT):
      super(LockboxDisplayModel, self).__init__()
      self.boxList = allLockboxes
      self.dateFmt = dateFormat
      self.main = main



   def recomputeMaxKeys(self):
      self.maxN = max([lbox.N for lbox in self.boxList])

   def rowCount(self, index=QtCore.QModelIndex()):
      return len(self.boxList)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 12

   def getKeyDisp(self, lbox, i):
      if len(lbox.commentList[i].strip())>0:
         return lbox.commentList[i]
      else:
         pubhex = binary_to_hex(lbox.pkList[i])
         addr = hash160_to_addrStr(lbox.a160List[i])
         return "%s (%s...)" % (addr, pubhex[:20])

   def data(self, index, role=QtCore.Qt.DisplayRole):
      row,col = index.row(), index.column()
      lbox = self.boxList[row]
      lbID = lbox.uniqueIDB58
      try:
         lwlt = self.main.cppLockboxWltMap[lbID]
      
         nTx, bal = 0, 0
         if TheBDM.getState()==BDM_BLOCKCHAIN_READY:
            nTx = lwlt.getWltTotalTxnCount()
            bal = lwlt.getFullBalance()
      except:
         nTx = "N/A"
         bal = "N/A"

      if role==QtCore.Qt.DisplayRole:
         if col==LOCKBOXCOLS.ID: 
            return lbID
         elif col==LOCKBOXCOLS.CreateDate: 
            return unixTimeToFormatStr(lbox.createDate, self.dateFmt)
         elif col==LOCKBOXCOLS.MSType: 
            return '%d-of-%d' % (lbox.M, lbox.N)
         elif col==LOCKBOXCOLS.LBName: 
            return lbox.shortName
         elif col==LOCKBOXCOLS.Key0: 
            return self.getKeyDisp(lbox, 0)
         elif col==LOCKBOXCOLS.Key1: 
            return self.getKeyDisp(lbox, 1)
         elif col==LOCKBOXCOLS.Key2: 
            return self.getKeyDisp(lbox, 2)
         elif col==LOCKBOXCOLS.Key3: 
            return self.getKeyDisp(lbox, 3)
         elif col==LOCKBOXCOLS.Key4: 
            return self.getKeyDisp(lbox, 4)
         elif col==LOCKBOXCOLS.NumTx: 
            if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
               return '(...)'
            return nTx
         elif col==LOCKBOXCOLS.Balance: 
            if not TheBDM.getState()==BDM_BLOCKCHAIN_READY:
               return '(...)'
            
            if lbox.isEnabled == True:
               if isinstance(bal, str):
                  return bal
               return coin2str(bal, maxZeros=2)
            
            scanStr = self.tr('Scanning: %d%%' % self.main.walletSideScanProgress[lbID])
            return scanStr
            
         elif col==LOCKBOXCOLS.UnixTime: 
            return str(lbox.createDate)

      elif role==QtCore.Qt.TextAlignmentRole:
         if col in (LOCKBOXCOLS.MSType, 
                    LOCKBOXCOLS.NumTx,
                    LOCKBOXCOLS.Balance):
            return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

         return int(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)

      elif role==QtCore.Qt.FontRole:
         f = GETFONT('Var')
         if col==LOCKBOXCOLS.Balance:
            f = GETFONT('Fixed')
         if nTx>0:
            f.setWeight(QtGui.QFont.Bold)
         return f
      elif role==QtCore.Qt.BackgroundColorRole:
         if bal>0:
            return Colors.SlightGreen

      return None


   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      colLabels = ['ID', 'Type', 'Created', 'Info', 
                   'Key #1', 'Key #2', 'Key #3', 'Key #4', 'Key #5', 
                   '#Tx', 'Funds', 'UnixTime']
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            return colLabels[section]
      elif role==QtCore.Qt.TextAlignmentRole:
         return int(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)


   def flags(self, index, role=QtCore.Qt.DisplayRole):
      if role == QtCore.Qt.DisplayRole:
         lbox = self.boxList[index.row()]
         
         rowFlag = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable
         
         if lbox.isEnabled is False:      
            return QtCore.Qt.ItemFlags()      
            
         return rowFlag      


class LockboxDisplayProxy(QtCore.QSortFilterProxyModel):
   """
   A proxy that re-maps indices to the table view so that data appears
   sorted without touching the model 
   """
   def lessThan(self, idxLeft, idxRight):
      COL = LOCKBOXCOLS
      thisCol  = self.sortColumn()

      def getDouble(idx, col):
         row = idx.row()
         s = toUnicode(self.sourceModel().index(row, col).data().toString())
         return float(s)

      def getInt(idx, col):
         row = idx.row()
         s = toUnicode(self.sourceModel().index(row, col).data().toString())
         return int(s)

      #LOCKBOXCOLS = enum('ID', 'MSType', 'CreateDate', 'LBName', \
                     #'Key0', 'Key1', 'Key2', 'Key3', 'Key4', \
                     #'NumTx', 'Balance', 'UnixTime')


      strLeft  = str(self.sourceModel().data(idxLeft).toString())
      strRight = str(self.sourceModel().data(idxRight).toString())

      if thisCol in (COL.ID, COL.MSType, COL.LBName):
         return (strLeft.lower() < strRight.lower())
      elif thisCol==COL.CreateDate:
         tLeft  = getDouble(idxLeft,  COL.UnixTime)
         tRight = getDouble(idxRight, COL.UnixTime)
         return (tLeft<tRight)
      elif thisCol==COL.NumTx:
         if TheBDM.getState()==BDM_BLOCKCHAIN_READY:
            ntxLeft  = getInt(idxLeft,  COL.NumTx)
            ntxRight = getInt(idxRight, COL.NumTx)
            return (ntxLeft < ntxRight)
      elif thisCol==COL.Balance:
         if TheBDM.getState()==BDM_BLOCKCHAIN_READY:
            btcLeft  = getDouble(idxLeft,  COL.Balance)
            btcRight = getDouble(idxRight, COL.Balance)
            return (abs(btcLeft) < abs(btcRight))

      return super(LockboxDisplayProxy, self).lessThan(idxLeft, idxRight)









