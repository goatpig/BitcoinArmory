##############################################################################
#                                                                            #
# Copyright (C) 2016-2024, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################
import binascii

from qtpy import QtCore

from armoryengine.ArmoryUtils import coin2str, getNameForAddrType
from armoryengine.AddressUtils import addrStr_to_hash160
from armoryengine.BDM import TheBDM
from armoryengine.Transaction import PyTx, getFeeForTx
from armoryengine.CppBridge import TheBridge

from qtdialogs.qtdefines import GETFONT
from armorycolors import Colors

COL_TREE = 0
COL_COMMENT = 1
COL_COUNT = 2
COL_BALANCE = 3

COL_NAME = 0
COL_TXOUTCOUNT = 1
COL_VALUE = 2
COL_DESCR = 3

COL_OUTPUT = 0
COL_ADDR = 1

################################################################################
class AddressObjectItem(object):

   def __init__(self, addrObj):
      self.addrObj = addrObj

   def rowCount(self):
      return 0

   def hasEntries(self):
      return False

   def getName(self):
      return self.addrObj.getAddressString()

   def getCount(self):
      return self.addrObj.getTxioCount()

   def getBalance(self):
      return self.addrObj.getFullBalance()

   def getComment(self):
      return self.addrObj.getComment()

   def getAddrObj(self):
      return self.addrObj

   def canDoubleClick(self):
      return True

################################################################################   
class CoinControlUtxoItem():

   def __init__(self, parent, utxo):
      self.utxo = utxo
      self.parent = parent

      if utxo.getTxHeight() == 2**32 - 1:
         self.name = QtCore.QObject().tr("ZC ID: %s | TxOut: %s" % \
            (str(utxo.getTxIndex()), \
            str(utxo.getTxOutIndex())))
      else:
         self.name = QtCore.QObject().tr("Block: #%s | Tx: #%s | TxOut: #%s" % \
            (str(utxo.getTxHeight()), \
            str(utxo.getTxIndex()), \
            str(utxo.getTxOutIndex())))

      self.state = QtCore.Qt.Checked
      if utxo.isChecked() == False:
         self.state = QtCore.Qt.Unchecked

      self.comment = self.parent.getCommentFromWallet(self.utxo.getTxHash())

   def rowCount(self):
      return 0

   def hasEntries(self):
      return False

   def getName(self):
      return self.name

   def getBalance(self):
      return self.utxo.getValue()

   def getComment(self):
      return self.comment

   def checked(self):
      return self.state

   def setCheckState(self, val):
      self.checkDown(val)

      try:
         self.parent.checkUp()
      except:
         pass

   def checkDown(self, val):
      self.state = QtCore.Qt.CheckState(val)
      if self.state == QtCore.Qt.Checked:
         self.utxo.setChecked(True)
      else:
         self.utxo.setChecked(False)

################################################################################
class RBFutxoItem():

   def __init__(self, parent, utxo):
      self.utxo = utxo
      self.parent = parent
      if utxo.getTxHeight() == 2**32 - 1:
         self.name = QtCore.QObject().tr("ZC id: %s | TxOut: %s" % \
            (str(utxo.getTxIndex()), \
            str(utxo.getTxOutIndex())))
      else:
         self.name = QtCore.QObject().tr("Block: #%s | Tx: #%s | TxOut: #%s" % \
            (str(utxo.getTxHeight()), \
            str(utxo.getTxIndex()), \
            str(utxo.getTxOutIndex())))

      self.state = QtCore.Qt.Checked
      if utxo.isChecked() == False:
         self.state = QtCore.Qt.Unchecked

      self.addrStr = TheBridge.scriptUtils.getAddrStrForScrAddr(
         utxo.getRecipientScrAddr())
      self.value = coin2str(utxo.getValue(), maxZeros=2)

   def rowCount(self):
      return 0

   def hasEntries(self):
      return False

   def getName(self):
      return self.name

   def getValue(self):
      return self.value

   def getAddress(self):
      return self.addrStr

   def checked(self):
      return self.state

   def setCheckState(self, val):
      self.state = val

   def isCheckable(self):
      return True

################################################################################
class EmptyNode(object):

   def __init__(self):
      self.name = "None"

   def rowCount(self):
      return 0

   def hasEntries(self):
      return False

   def getName(self):
      return self.name

   def canDoubleClick(self):
      return False

   def getComment(self):
      return ""

################################################################################
class TreeNode(object):

   def __init__(self, parent, name, isExpendable=False):
      self.name = name
      self.isExpendable = isExpendable
      self.parent = parent

      self.populated = False

      self.entries = []
      self.empty = False
      self.checkStatus = self.computeState()

   def rowCount(self):
      self.populate()
      if not self.empty:
         return len(self.entries)
      else:
         return 1

   def hasEntries(self):
      return self.isExpendable

   def appendEntry(self, entry):
      self.entries.append(entry)
      self.populated = True

   def getEntryByIndex(self, index):
      self.populate()
      if not self.empty:
         return self.entries[index]
      else:
         return EmptyNode()

   def checked(self):
      return self.checkStatus

   def checkDown(self, val):
      #set own state
      self.checkStatus = val

      #set children
      self.populate()
      for entry in self.entries:
         entry.checkDown(val)

   def checkUp(self):
      #figure out own state
      self.checkStatus = self.computeState()

      #checkUp on parent
      try:
         self.parent.checkUp()
      except:
         pass

   def setCheckState(self, val):
      self.checkDown(val)

      try:
         self.parent.checkUp()
      except:
         pass

   def computeState(self):
      if not self.hasEntries():
         raise Exception("node needs children to compute state")

      self.populate()
      state = QtCore.Qt.Checked
      try:
         state = self.entries[0].checked()
         for i in range(1, len(self.entries)):
            if self.entries[i].checked() != state:
               state = QtCore.Qt.PartiallyChecked
               break
      except:
         pass

      return state

   def getName(self):
      return self.name


################################################################################  
class CoinControlAddressItem(TreeNode):

   def __init__(self, parent, name, utxoList):
      self.utxoList = utxoList
      super(CoinControlAddressItem, self).__init__(parent, name, True)

      self.balance = 0
      for utxo in utxoList:
         self.balance += utxo.getValue()

      self.comment = self.parent.getCommentFromWallet(name)

   def rowCount(self):
      return len(self.utxoList)
   
   def populate(self):
      if self.populated == True:
         return

      self.entries = []
      for utxo in self.utxoList:
         self.entries.append(CoinControlUtxoItem(self, utxo))

      self.populated = True

   def getBalance(self):
      return self.balance

   def getComment(self):
      return self.comment

   def getCount(self):
      txout_count = len(self.utxoList)
      if txout_count == 1:
         return None
      return txout_count

   def getCommentFromWallet(self, val):
      return self.parent.getCommentFromWallet(val)


################################################################################
class AddressTreeNode(TreeNode):

   def __init__(self, addrType, isExpendable=False, populateMethod=None):
      self.populateMethod = populateMethod
      self.addrType = addrType

      if type(addrType) != str:
         name = getNameForAddrType(addrType)
      else:
         name = addrType
         self.populateMethod = None
         self.addrType = None

      super(AddressTreeNode, self).__init__(None, name, isExpendable)

   def populate(self):
      if self.populated:
         return

      if self.populateMethod == None:
         return

      addrList = self.populateMethod(self.addrType)
      if len(addrList) > 0:
         for addr in addrList:
            self.entries.append(AddressObjectItem(addr))
      else:
         self.empty = True

      self.populated = True

   def getBalance(self):
      self.populate()

      balance = 0
      for entry in self.entries:
         balance += entry.getBalance()

      return balance

################################################################################
class CoinControlTreeNode(TreeNode):

   def __init__(self, parent, addrType, isExpendable=False, populateMethod=None):
      self.addrType = addrType
      if type(self.addrType) == str:
         self.name = self.addrType
      else:
         self.name = getNameForAddrType(self.addrType)

      self.populateMethod = populateMethod
      super(CoinControlTreeNode, self).__init__(parent, self.name, isExpendable)

   def getName(self):
      return self.name

   def populate(self):
      if self.populated:
         return

      if self.populateMethod == None:
         return

      utxoList = self.populateMethod(self.addrType)
      if len(utxoList) > 0:
         for addr in utxoList:
            self.entries.append(\
               CoinControlAddressItem(self, addr, utxoList[addr]))
      else:
         self.empty = True

      self.populated = True

   def getBalance(self):
      self.populate()

      balance = 0
      for entry in self.entries:
         if entry.checked() != QtCore.Qt.Unchecked:
            balance += entry.getBalance()

      return balance

   def getCount(self):
      if self.populateMethod == None:
         return None

      self.populate()
      return len(self.entries)

   def getCommentFromWallet(self, val):
      try:
         return self.parent.getCommentFromWallet(val)
      except:
         return ""


################################################################################
class RBFutxoTreeNode(TreeNode):

   def __init__(self, parent, utxoList):
      self.utxoList = utxoList
      name = QtCore.QObject().tr("Redeemed Outputs")
      super(RBFutxoTreeNode, self).__init__(parent, name, True)

   def populate(self):
      if self.populated == True:
         return

      self.entries = []
      for utxo in self.utxoList:
         self.entries.append(RBFutxoItem(self, utxo))

      self.populated = True

################################################################################    
class RBFspendTreeNode(TreeNode):

   def __init__(self, parent, txList):
      self.txList = txList

      name = QtCore.QObject().tr("Spender Tx")
      super(RBFspendTreeNode, self).__init__(parent, name, True)

   def populate(self):
      if self.populated == True:
         return

      self.entries = []
      if len(self.txList):
         for tx in self.txList:
            self.entries.append(RBFTxTreeNode(self, tx))
      else:
         self.entries.append(EmptyNode())

      self.populated = True

################################################################################
class RBFTxTreeNode(TreeNode):

   def __init__(self, parent, txhash, entryList):
      self.entryList = entryList
      name = QtCore.QObject().tr("Tx: %s" % txhash)
      #fee, fee_byte = getFeeForTx(txhash)
      #self.value = QtCore.QObject().tr("Fee: %1 sat. (%2 sat/B)").arg(\
      #                        unicode(fee), unicode(fee_byte))
      super(RBFTxTreeNode, self).__init__(parent, name, True)

   def getValue(self):
      return self.value

   def rowCount(self):
      return 2

   def populate(self):
      if self.populated == True:
         return

      self.entries = []
      
      utxoList = []
      txList = []

      for entry in self.entryList:
         if isinstance(entry, list):
            txList.append(entry)
         else:
            utxoList.append(entry)

      #order by utxos and transactions
      self.entries.append(RBFutxoTreeNode(self, utxoList))
      self.entries.append(RBFspendTreeNode(self, txList))

      self.populated = True

################################################################################     
class RBFTreeNode(TreeNode):

   def __init__(self, parent, name, isExpendable=False, populateMethod=None):
      self.populateMethod = populateMethod
      super(RBFTreeNode, self).__init__(parent, name, isExpendable)

   def populate(self):
      if self.populated:
         return

      if self.populateMethod == None:
         return

      rbfDict = self.populateMethod()
      if len(rbfDict) > 0:
         for txhash in rbfDict:
            utxoList = rbfDict[txhash]
            self.entries.append(RBFTxTreeNode(self, txhash, utxoList))

      else:
         self.empty = True

      self.populated = True

   def getCount(self):
      if self.populateMethod == None:
         return None

      self.populate()
      return len(self.entries)

################################################################################
class TreeStructure_AddressDisplay():

   def __init__(self, wallet, parent_qobj):
      self.wallet = wallet
      self.root = None
      self.parent_qobj = parent_qobj

      self.setup()

   def setup(self):
      #create root node
      self.root = AddressTreeNode("root", True, None)

      def createChildNode(name, filterStr):
         addrCount = self.wallet.getFilteredAddrCount(filterStr, None)
         name += " (" + str(addrCount) + ")"
         nodeMain = AddressTreeNode(name, True, None)

         def walletFilter(addrType):
            return self.wallet.returnFilteredAddrList(filterStr, addrType)

         for addrType in self.wallet.getAddressTypes():
            nodeAddr = AddressTreeNode(addrType, True, walletFilter)
            nodeMain.appendEntry(nodeAddr)

         return nodeMain

      #create top 3 nodes
      nodeUsed   = createChildNode(self.parent_qobj.tr("Used Addresses"), "Used")
      nodeChange = createChildNode(self.parent_qobj.tr("Change Addresses"), "Change")
      nodeUnused = createChildNode(self.parent_qobj.tr("Unused Addresses"), "Unused")

      self.root.appendEntry(nodeUsed)
      self.root.appendEntry(nodeChange)
      self.root.appendEntry(nodeUnused)

      #if we have imports, add an import section
      if not self.wallet.hasImports():
         return

      nodeImports = AddressTreeNode(
         'Imports', True, \
         self.wallet.getImportCppAddrList)

      self.root.appendEntry(nodeImports)

################################################################################
class TreeStructure_CoinControl():

   def __init__(self, wallet):
      self.wallet = wallet
      self.root = None

      self.setup()

   def getTreeData(self):
      return self.treeData

   def reset(self):
      for section in self.treeData['UTXO']:
         addrDict = self.treeData['UTXO'][section]

         for addr in addrDict:
            utxoList = addrDict[addr]
            for utxo in utxoList:
               utxo.setChecked(True)

   def setup(self):
      addrTypes = self.wallet.getAddressTypes()

      self.treeData = {
         'UTXO'   : dict(),
         'RBF'    : dict(),
         'CPFP'   : dict()
      }

      utxoDict = self.treeData['UTXO']
      for addrType in addrTypes:
         utxoDict[addrType] = dict()

      #populate utxos
      utxoList = self.wallet.getFullUTXOList()

      #arrange by script type
      for utxo in utxoList:
         binAddr = utxo.getRecipientScrAddr()
         addrObj = self.wallet.getAddrByHash(binAddr)
         if addrObj == None:
            continue

         addrStr = addrObj.getAddressString()
         addrType = addrObj.addrType

         addrDict = self.treeData['UTXO'][addrType]
         if not addrStr in addrDict:
            addrDict[addrStr] = []
         addrDict[addrStr].append(utxo)

      #populate cpfp
      cpfpList = self.wallet.getZCUTXOList()
      addrDict = self.treeData['CPFP']
      for cpfp in cpfpList:
         binAddr = cpfp.getRecipientScrAddr()
         addrStr = TheBridge.scriptUtils.getAddrStrForScrAddr(binAddr)

         if not addrStr in addrDict:
            addrDict[addrStr] = []
         addrDict[addrStr].append(cpfp)

      #create root node
      self.root = CoinControlTreeNode(self, "root", True, None)

      def createUtxoNode(name):
         nodeMain = CoinControlTreeNode(self, name, True, None)
         addrTypes = self.wallet.getAddressTypes()

         def ccFilter(addrType):
            return self.treeData['UTXO'][addrType]

         for addrType in addrTypes:
            node = CoinControlTreeNode(nodeMain, addrType, True, ccFilter)
            nodeMain.appendEntry(node)

         return nodeMain

      def createCPFPNode(name):
         def ccCPFP(_):
            return self.treeData['CPFP']

         nodeMain = CoinControlTreeNode(None, name, True, ccCPFP)

         return nodeMain

      #create top 3 nodes
      nodeUTXO = createUtxoNode(QtCore.QObject().tr("Unspent Outputs"))
      #nodeRBF = createChildNode(self.main.tr("RBF Eligible"), "RBF")
      nodeCPFP = createCPFPNode(QtCore.QObject().tr("CPFP Eligible Outputs"))

      self.root.appendEntry(nodeUTXO)
      #self.root.appendEntry(nodeRBF)
      self.root.appendEntry(nodeCPFP)
      nodeCPFP.setCheckState(QtCore.Qt.Unchecked)
      self.root.checkStatus = self.root.computeState()

   def getCommentFromWallet(self, val):
      if len(val) != 32:
         try:
            prefix, val = addrStr_to_hash160(val)
         except: 
            pass
      return self.wallet.getComment(val)

################################################################################
class TreeStructure_RBF():

   def __init__(self, wallet):
      self.wallet = wallet
      self.root = None

      self.setup()

   def getTreeData(self):
      return self.rbfDict

   def setup(self):
      rbfList = self.wallet.getRBFTxOutList()

      self.rbfDict = {}

      #order outputs by parent hash
      for utxo in rbfList:
         parentHash = utxo.getTxHashStr()
         if not parentHash in self.rbfDict:
            self.rbfDict[parentHash] = []

         utxoList = self.rbfDict[parentHash]
         utxoList.append(utxo)

      for txHashStr in self.rbfDict:
         #get outpoints for spender tx
         entryList = self.rbfDict[txHashStr]
         txHash = binascii.unhexlify(txHashStr)
         cppTx = TheBridge.service.getTxByHash(txHash)

         if cppTx is not None:
            pytx = PyTx().unserialize(cppTx.raw)
         else:
            continue

         for _input in pytx.inputs:
            spentHash = _input.outpoint.getTxHashStr()

            #if this tx redeems an output in our list of RBF tx,
            #link it to the spendee
            if spentHash in self.rbfDict:
               spendeeList = self.rbfDict[spentHash]
               spendeeList.append([txHashStr, entryList])

      def getRBFDict():
         return self.rbfDict

      self.root = RBFTreeNode(None, "root", True, getRBFDict)

################################################################################
class NodeItem(object):

   def __init__(self, row, parent, treeNode):
      self.parent = parent
      self.row = row
      self.treeNode = treeNode

      self.children = {}

      if parent != None:
         self.depth = parent.depth + 1
         parent.addChild(self)
      else:
         self.depth = 0

   def addChild(self, child):
      self.children[child.row] = child

   def hasChildren(self):
      return self.treeNode.hasEntries()

   def getChildAtRow(self, row):
      try:
         node = self.children[row]
      except:
         node = NodeItem(row, self, self.treeNode.getEntryByIndex(row))

      return node

   def rowCount(self):
      return self.treeNode.rowCount()

   def canDoubleClick(self):
      try:
         return self.treeNode.canDoubleClick()
      except:
         return False

################################################################################      
class ArmoryTreeModel(QtCore.QAbstractItemModel):

   def __init__(self, main):
      super(ArmoryTreeModel, self).__init__()
      self.main = main

   def parent(self, index):
      if not index.isValid():
         return QtCore.QModelIndex()

      node = self.getNodeItem(index)
      if node is None:
         return QtCore.QModelIndex()

      parent = node.parent
      if parent is None:
         return QtCore.QModelIndex()

      return self.createIndex(parent.row, 0, parent)

   def index(self, row, column, parent):
      parentNode = self.getNodeItem(parent)
      return self.createIndex(row, column, parentNode.getChildAtRow(row))

   def hasChildren(self, parent):
      node = self.getNodeItem(parent)
      return node.hasChildren()

   def rowCount(self, index):
      node = self.getNodeItem(index)
      return node.rowCount()

   def getNodeItem(self, index):
      if index == None:
         return self.root

      if not index.isValid():
         return self.root

      item = index.internalPointer()
      if item is None:
         return self.root

      return item

################################################################################
class AddressTreeModel(ArmoryTreeModel):
   def __init__(self, main, wlt):
      super(AddressTreeModel, self).__init__(main)

      self.wlt = wlt

      self.treeStruct = TreeStructure_AddressDisplay(self.wlt, self)
      self.root = NodeItem(0, None, self.treeStruct.root)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 4

   def data(self, index, role=QtCore.Qt.DisplayRole):
      if role==QtCore.Qt.DisplayRole:
         col = index.column()
         node = self.getNodeItem(index)

         if col == COL_TREE:
            return node.treeNode.getName()

         if col == COL_BALANCE:
            try:
               return coin2str(node.treeNode.getBalance(), maxZeros=2)
            except:
               return None

         if node.hasChildren():
            return None

         if not node.treeNode.canDoubleClick():
            return None

         if col == COL_COMMENT:
            return node.treeNode.getComment()

         if col == COL_COUNT:
            return node.treeNode.getCount()

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COL_TREE: return self.tr('Address')
            if section==COL_COMMENT: return self.tr('Comment')
            if section==COL_COUNT:  return self.tr('Tx Count')
            if section==COL_BALANCE:  return self.tr('Balance')

      return None 

   def refresh(self):
      #this method repopulates the underlying tree view map, only use
      #when that data changed
      self.treeStruct = TreeStructure_AddressDisplay(self.wlt, self)
      self.root = NodeItem(0, None, self.treeStruct.root)


################################################################################ 
class CoinControlTreeModel(ArmoryTreeModel):
   def __init__(self, main, wlt):
      super(CoinControlTreeModel, self).__init__(main)

      self.wlt = wlt

      self.treeStruct = TreeStructure_CoinControl(self.wlt)
      self.root = NodeItem(0, None, self.treeStruct.root)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 4

   def data(self, index, role=QtCore.Qt.DisplayRole):
      col = index.column()
      node = self.getNodeItem(index)

      if role==QtCore.Qt.DisplayRole:
         if col == COL_NAME:
            return node.treeNode.getName()

         if col == COL_DESCR:
            try:
               return node.treeNode.getComment()
            except:
               pass

         if col == COL_VALUE:
            try:
               return coin2str(node.treeNode.getBalance(), maxZeros=2)
            except:
               pass

         if col == COL_TXOUTCOUNT:
            try:
               return node.treeNode.getCount()
            except:
               pass

      elif role==QtCore.Qt.CheckStateRole:
         try:
            if col == COL_NAME:
               st = node.treeNode.checked()
               return st
            else:
               return None
         except:
            pass

      elif role==QtCore.Qt.BackgroundRole:
         try:
            if node.treeNode.checked() != QtCore.Qt.Unchecked:
               return Colors.SlightBlue
         except:
            pass

      elif role==QtCore.Qt.FontRole:
         try:
            if node.treeNode.checked() != QtCore.Qt.Unchecked:
               return GETFONT('Fixed', bold=True)
         except:
            pass

      return None

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COL_NAME: return self.tr('Address/ID')
            if section==COL_DESCR:  return self.tr('Comment')
            if section==COL_VALUE:  return self.tr('Selected Balance/Value')
            if section==COL_TXOUTCOUNT: return self.tr('Count')

      return None

   def flags(self, index):
      f = QtCore.Qt.ItemIsEnabled
      if index.column() == 0:
         node = self.getNodeItem(index)
         if node.treeNode.getName() != 'None':
            f |= QtCore.Qt.ItemIsUserCheckable
      return f

   def setData(self, index, value, role):
      if role == QtCore.Qt.CheckStateRole:
         node = self.getNodeItem(index)
         node.treeNode.setCheckState(value)

         self.layoutChanged.emit()
         return True

      return False


################################################################################ 
class RBFTreeModel(ArmoryTreeModel):
   def __init__(self, main, wlt):
      super(RBFTreeModel, self).__init__(main)

      self.wlt = wlt
      self.treeStruct = TreeStructure_RBF(self.wlt)
      self.root = NodeItem(0, None, self.treeStruct.root)

   def columnCount(self, index=QtCore.QModelIndex()):
      return 3

   def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
      if role==QtCore.Qt.DisplayRole:
         if orientation==QtCore.Qt.Horizontal:
            if section==COL_OUTPUT: return self.tr('Output ID')
            if section==COL_ADDR:  return self.tr('Address')
            if section==COL_VALUE:  return self.tr('Value')

      return None

   def setData(self, index, value, role):
      if role == QtCore.Qt.CheckStateRole:
         node = self.getNodeItem(index)
         node.treeNode.setCheckState(value)

         self.layoutChanged.emit()
         return True

      return False

   def flags(self, index):
      f = QtCore.Qt.ItemIsEnabled
      if index.column() == 0:
         node = self.getNodeItem(index)
         try:
            if node.treeNode.isCheckable():
               f |= QtCore.Qt.ItemIsUserCheckable
         except:
            pass
      return f

   def data(self, index, role=QtCore.Qt.DisplayRole):
      col = index.column()
      node = self.getNodeItem(index)

      if role==QtCore.Qt.DisplayRole:
         if col == COL_OUTPUT:
            return node.treeNode.getName()

         if col == COL_ADDR:
            try:
               return node.treeNode.getAddress()
            except:
               pass

         if col == COL_VALUE:
            try:
               return node.treeNode.getValue()
            except:
               pass

      elif role==QtCore.Qt.CheckStateRole:
         try:
            if col == COL_OUTPUT:
               if node.treeNode.isCheckable():
                  st = node.treeNode.checked()
                  return st
            else:
               return None
         except:
            pass

      elif role==QtCore.Qt.BackgroundRole:
         try:
            if node.treeNode.checked() != QtCore.Qt.Unchecked:
               return Colors.SlightBlue
         except:
            pass

      elif role==QtCore.Qt.FontRole:
         try:
            if node.treeNode.checked() != QtCore.Qt.Unchecked:
               return GETFONT('Fixed', bold=True)
         except:
            pass

      return None
