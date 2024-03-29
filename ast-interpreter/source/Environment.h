//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include <iostream>

using namespace clang;
using namespace std;

//#define DEBUG 1

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an long value)
   std::map<Decl*, long> mVars;
   std::map<Stmt*, long> mExprs;
   /// The current stmt
   Stmt * mPC;
   
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(Decl* decl, long val) {
      mVars[decl] = val;
   }    
   int getDeclVal(Decl * decl) {
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   void bindStmt(Stmt * stmt, long val) {
	   mExprs[stmt] = val;
   }
   int getStmtVal(Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   Stmt * getPC() {
	   return mPC;
   }

};

/// Heap maps address to a value

class Heap {
	std::map<long,long> mBufs;//map the address to size
	std::map<long,long> mContents; //map the address to value
public:
	Heap():mBufs(),mContents(){
	}
	long Malloc(int size){
		int *buf=(int *)malloc(size);
		mBufs.insert(std::make_pair((long)buf,size));
		for(int i=0;i<size;i++){
			mContents.insert(std::make_pair((long)(buf+i),0));
		}
		return (long)buf;
	}

	long Free(long addr){
		assert (mBufs.find(addr) != mBufs.end());
		int * buf = (int *)addr;
      	long size = mBufs.find(addr)->second;
     	 mBufs.erase(mBufs.find(addr));

      	for (int i = 0; i < size; i++) {
      		assert (mContents.find((long)(buf+i)) != mContents.end());
        	mContents.erase((long)(buf+i));
      		}
		
		free(buf);
	}

	// update the value of addr in the buf
	void Update(long addr, long val) {
		while((addr%4)!=0) addr=addr+1;
      	assert (mContents.find(addr) != mContents.end());
      	mContents[addr] = val;
   }

   //get the value of addr in the buf
   	long Get(long addr) {
	   	while((addr%4)!=0) addr=addr+1;
      	assert (mContents.find(addr) != mContents.end());
      	return mContents.find(addr)->second;
    }

};


class Environment {
   	std::vector<StackFrame> mStack;
  	std::vector<StackFrame> mVarGlobal;  /// Store the global var
   	Heap mHeap;

	FunctionDecl * mFree;				/// Declartions to the built-in functions
	FunctionDecl * mMalloc;
	FunctionDecl * mInput;
	FunctionDecl * mOutput;

	FunctionDecl * mEntry;
	bool Returnflag=false;             
public:
	Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
	}
   
    bool isReturn(){                   /// Represent the current function call is returned or not
	  return Returnflag;
   	}
   	void setReturn(){                  ///  used when a function call begin or return 
	   Returnflag=!Returnflag;
   	}


   /// Initialize the Environment
	void init(TranslationUnitDecl * unit) {
		for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
			if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
				if (fdecl->getName().equals("FREE")) mFree = fdecl;
				else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
				else if (fdecl->getName().equals("GET")) mInput = fdecl;
				else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
				else if (fdecl->getName().equals("main")) mEntry = fdecl;
			}

		   	/// Process the global var decl
			/// !TODO Support global array decl
		 	if(VarDecl *vardecl=dyn_cast<VarDecl>(*i)){
		 		if( !(vardecl->hasInit()) ){
		 			StackFrame stack;
		 			stack.bindDecl(vardecl,0);
		 			mVarGlobal.push_back(stack);
		 		}
		 		else if( vardecl->hasInit()){
		 			if(isa<IntegerLiteral>(vardecl->getInit()))
              		{
                  		StackFrame stack;
                  		IntegerLiteral *integer=dyn_cast<IntegerLiteral>(vardecl->getInit());
                  		int val=integer->getValue().getSExtValue();
                  		stack.bindDecl(vardecl, val);
                  		mVarGlobal.push_back(stack);
              		}
		 		}
		 	}
	   	}
	   mStack.push_back(StackFrame());
   }



   FunctionDecl * getEntry() {
	   return mEntry;
   }

   /// !TODO Support comparison operation
	void binop(BinaryOperator *bop) {
		Expr * left = bop->getLHS();
		Expr * right = bop->getRHS();
		int valLeft=mStack.back().getStmtVal(left);
		int valRight=mStack.back().getStmtVal(right);
       
	   	if (bop->isAssignmentOp()) {
		   	if(isa<ArraySubscriptExpr>(left))
	   		{
				ArraySubscriptExpr *array=dyn_cast<ArraySubscriptExpr>(left);
				Expr *base_expr=array->getBase();
				//get the base of the array
				long base=mStack.back().getStmtVal(base_expr);
				Expr *offset_expr=array->getIdx();
				//get the offset index of the array, here is an integerliteral
				long offset=mStack.back().getStmtVal(offset_expr);
				mHeap.Update(base + offset*sizeof(int), valRight);
			}
		
			if(isa<UnaryOperator>(left)){
				UnaryOperator* uop= dyn_cast<UnaryOperator>(left);
				if((uop->getOpcode())==UO_Deref){  /// *a
					Expr* expr=uop->getSubExpr();
					long addr=mStack.back().getStmtVal(expr);
					mHeap.Update(addr,valRight);
				}
			}
		   mStack.back().bindStmt(left, valRight);
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
			   Decl * decl = declexpr->getFoundDecl();
			   this->bindDecl(decl, valRight);

		   }
		}
		

		if (bop->isAdditiveOp()) {
			switch(bop->getOpcode())
	   		{
		   		//+
		   		case BO_Add:
		   		mStack.back().bindStmt(bop,valLeft+valRight);
		   		break;
		   		//-
		   		case BO_Sub:
		   		mStack.back().bindStmt(bop,valLeft-valRight);
		   		break;
	   		}
	   	}
	   	
	   	if(bop->isMultiplicativeOp()){
	   		switch(bop->getOpcode())
	   		{
	   		//*
	   			case BO_Mul:
	   			mStack.back().bindStmt(bop,valLeft * valRight);
	   			break;
	   		}
	   	}

		if(bop->isComparisonOp ()){
	   		switch(bop->getOpcode())
	   		{
	   			case BO_LT: /// <
	   				if( valLeft < valRight )
	   					mStack.back().bindStmt(bop,true);
	   				else
	   					mStack.back().bindStmt(bop,false);
	   				break;
		   		case BO_GT: /// >
			   		if( valLeft > valRight )
			   			mStack.back().bindStmt(bop,true);
			   		else
			   			mStack.back().bindStmt(bop,false);
			   		break;
		   		//>=
		   		case BO_GE:
			   		if( valLeft >= valRight )
			   			mStack.back().bindStmt(bop,true);
			   		else
			   			mStack.back().bindStmt(bop,false);
			   		break;
		   		//<=
		   		case BO_LE:
			   		if( valLeft <= valRight )
			   			mStack.back().bindStmt(bop,true);
			   		else
			   			mStack.back().bindStmt(bop,false);
			   		break;
		   		//==
		   		case BO_EQ:
			   		if( valLeft == valRight )
			   			mStack.back().bindStmt(bop,true);
			   		else
			   			mStack.back().bindStmt(bop,false);
			   		break;
		   		//!=
		   		case BO_NE:
			   		if( valLeft != valRight )
			   			mStack.back().bindStmt(bop,true);
			   		else
			   			mStack.back().bindStmt(bop,false);
			   		break;
		   		default:
			   		cout<<" invalid input comparisons! "<<endl;
			   		break;
	   		}
	   }
}
	   
   
   void unaryop(UnaryOperator *uop){	   
		Expr * expr=uop->getSubExpr();
		long val=mStack.back().getStmtVal(expr);
		switch(uop->getOpcode()){
			case UO_Plus: // +a
				mStack.back().bindStmt(uop,val);
				break;
			case UO_Minus: // -a
				mStack.back().bindStmt(uop,-val);
				break;
			case UO_Deref: // *a
				mStack.back().bindStmt(uop,mHeap.Get(val));
				break;
	   }
   }
   

   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
		   		if ( !(vardecl->hasInit()) ){ /// If the var is not initialized
		   			if( !(vardecl->getType()->isArrayType()) ){
						mStack.back().bindDecl(vardecl, 0);
		   			}
		   			else{//Array type
		   				int size=sizeof(int);
		   				const auto&array=dyn_cast<ArrayType>(vardecl->getType());
		   				if(array->getElementType()->isIntegerType()) size=sizeof(int);
		   				if(array->getElementType()->isCharType()) size=sizeof(char);
		   				string type=(vardecl->getType()).getAsString();
						//we use the naive method to get size of the array by string match
						//this method may be unsafe and unreusable
			 			int indexLeft=type.find("[");
			 			int indexRight=type.find("]");
			 			if((indexLeft!=string::npos) && (indexRight!=string::npos))
			 			{
			 				string num=type.substr(indexLeft+1,indexRight-indexLeft-1);
    						size*=atoi(num.c_str());
			 			}
		   				long buf=mHeap.Malloc(size);
		   				mStack.back().bindDecl(vardecl,buf);
		   			}

		   		}
		   		else if (vardecl->hasInit()){
                    if(isa<IntegerLiteral>(vardecl->getInit())){
                        IntegerLiteral *integer=dyn_cast<IntegerLiteral>(vardecl->getInit());
                        int val=integer->getValue().getSExtValue();
                        mStack.back().bindDecl(vardecl,val);

                    }
                    else{
                        int val=mStack.back().getStmtVal(vardecl->getInit()); 
                        mStack.back().bindDecl(vardecl, val);
                    }
                    
		   		}

		   }
	   }
   }
   
   	void declref(DeclRefExpr * declref) {
	   	#ifdef DEBUG
			std::cout<<"enter declref"<<std::endl;
		#endif
	  	mStack.back().setPC(declref);
	  	mStack.back().bindStmt(declref,0);
	  	Decl* decl = declref->getFoundDecl();
		int val = this->getDeclVal(decl);
		mStack.back().bindStmt(declref, val);
   }

   	void cast(CastExpr * castexpr) {
		mStack.back().setPC(castexpr);
		Expr * expr = castexpr->getSubExpr();
		if (castexpr->getType()->isIntegerType()){
			int val = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, val );
		}
		else{
			long val = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, val );
		}
  	}

   void call(CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : \n";
		  scanf("%d", &val);

		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val<<"\n";
	   } else if (callee == mMalloc){
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   //std::cout<<val<<std::endl;
		   long buf=mHeap.Malloc(val);
		   mStack.back().bindStmt(callexpr,buf);
	   } else if(callee == mFree){
		   Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   mHeap.Free(val);
		   mStack.back().bindStmt(callexpr,0);
	   }
	   else{
			StackFrame stack;
			auto param=callee->param_begin();
			for(CallExpr::arg_iterator it=callexpr->arg_begin(), ie=callexpr->arg_end();it!=ie;++it,++param){
				int val = mStack.back().getStmtVal(*it);
				stack.bindDecl(*param,val);
			}
			mStack.push_back(stack);
			#ifdef DEBUG
			std::cout<<"leave call "<<std::endl;
			#endif
		}		   
	}
	   
   
   
 	void ret(ReturnStmt* retstmt){
			#ifdef DEBUG
				std::cout<<"enter ret "<<std::endl;
			#endif
			Expr* expr=retstmt->getRetValue();
			long val = mStack.back().getStmtVal(expr);
			#ifdef DEBUG
				std::cout<<"val of ret "<<val<<std::endl;
			#endif
			mStack.pop_back();
			Stmt * stmt =mStack.back().getPC();
			mStack.back().bindStmt(stmt,val);
   }

   void typetrait(UnaryExprOrTypeTraitExpr* type){ /// process sizeof operator
	   	if(type->getTypeOfArgument()->isCharType()){
		   	mStack.back().bindStmt(type,sizeof(char));
	   	}
		else{
		   	mStack.back().bindStmt(type,sizeof(int));
	   	}
   	}

   void integerliteral(IntegerLiteral* integer){
   		int val=integer->getValue().getSExtValue();
   		mStack.back().bindStmt(integer,val);
   }
   	void array(ArraySubscriptExpr *arrayexpr){
		Expr *base_expr=arrayexpr->getBase();
		const PointerType* base_ptr=dyn_cast<PointerType>(base_expr->getType());

		/// get the lenth of different type
		int len=0;
		if(base_ptr->getPointeeType()->isCharType()) len=sizeof(char);
		if(base_ptr->getPointeeType()->isIntegerType()) len=sizeof(int);
		if(base_ptr->getPointeeType()->isPointerType()) len=sizeof(int);

		int base=mStack.back().getStmtVal(base_expr); 
		Expr *offset_expr=arrayexpr->getIdx();
		int offset=mStack.back().getStmtVal(offset_expr);

		mStack.back().bindStmt(arrayexpr,mHeap.Get(base + offset*sizeof(int)));
   	}
   
   	void paren(ParenExpr *paren){  ///process ()
	   	Expr* expr=paren->getSubExpr();
	   	long val = mStack.back().getStmtVal(expr);
	   	mStack.back().bindStmt(paren,val);
   	}



   	void bindDecl(Decl* decl, int val){  /// another implementation of bindDecl, to support the bind of global var
   		if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
   			if( !(vardecl->isLocalVarDeclOrParm()) ){
   				mVarGlobal.back().bindDecl(decl,val);
   			}
   			else{
   				mStack.back().bindDecl(decl, val);
   			}
   		}
   		else{
   				mStack.back().bindDecl(decl, val);
   		}		
   }

	int getDeclVal(Decl* decl){
		int val;
		if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
			if( !(vardecl->isLocalVarDeclOrParm()) ){
				val=mVarGlobal.back().getDeclVal(decl);
			}
			else{
				val = mStack.back().getDeclVal(decl);
			}
		}
		else{
			val = mStack.back().getDeclVal(decl);
		} 
		return val;

   	}

   	bool getcond(Expr *expr){
   		return mStack.back().getStmtVal(expr);
   }
};


