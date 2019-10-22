
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace std;

#include "Environment.h"
//#define DEBUG 1
class InterpreterVisitor : 
   	public EvaluatedExprVisitor<InterpreterVisitor> {
public:
	explicit InterpreterVisitor(const ASTContext &context, Environment * env)
	: EvaluatedExprVisitor(context), mEnv(env) {}
	virtual ~InterpreterVisitor() {}

	virtual void VisitBinaryOperator (BinaryOperator * bop) {
		#ifdef DEBUG
			std::cout<<"Enter BOP"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		VisitStmt(bop);
		mEnv->binop(bop);
   	}

   /// -a,*a
   virtual void VisitUnaryOperator (UnaryOperator * uop){
	   	#ifdef DEBUG
			std::cout<<"Enter UOP"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		VisitStmt(uop);
		mEnv->unaryop(uop);
   }

   virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
		#ifdef DEBUG
			std::cout<<"Enter DeclRefExpr"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		VisitStmt(expr);
		mEnv->declref(expr);
		#ifdef DEBUG
			std::cout<<"Leave DeclRefExpr"<<std::endl;
		#endif
   }

   virtual void VisitCastExpr(CastExpr * expr) {
	   	#ifdef DEBUG
			std::cout<<"Enter CAST"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		VisitStmt(expr);
		mEnv->cast(expr);
   }

   virtual void VisitCallExpr(CallExpr * call) {
	   	#ifdef DEBUG
			std::cout<<"Enter CALL"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		VisitStmt(call);
		mEnv->call(call);
		FunctionDecl * callee = call->getDirectCallee();
		if(callee->hasBody()){                                                
        	VisitStmt(callee->getBody());
			mEnv->setReturn();
       	}
	}

   	virtual void VisitDeclStmt(DeclStmt * declstmt) {
	   	#ifdef DEBUG
			std::cout<<"Enter DECL"<<std::endl;
		#endif
	   	if(mEnv->isReturn()) return;
	   	mEnv->decl(declstmt);
   	}
   	virtual void VisitIntegerLiteral(IntegerLiteral* integer){
	   	#ifdef DEBUG
			std::cout<<"Enter IntegerLiteral"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		mEnv->integerliteral(integer);
		#ifdef DEBUG
			std::cout<<"Leave IntegerLiteral"<<std::endl;
		#endif
   }

   virtual void VisitArraySubscriptExpr(ArraySubscriptExpr *arrayexpr){
		#ifdef DEBUG
			std::cout<<"Enter ArraySubscriptExpr"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		VisitStmt(arrayexpr);
		mEnv->array(arrayexpr);
   }

   virtual void VisitIfStmt(IfStmt* ifstmt){
	   	#ifdef DEBUG
			std::cout<<"Enter IF"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		Expr *expr=ifstmt->getCond();
		Visit(expr);
		bool cond=mEnv->getcond(expr);
		if(cond){
			if(isa<BinaryOperator>(ifstmt->getThen())){
                BinaryOperator * bop = dyn_cast<BinaryOperator>(ifstmt->getThen());
                this->VisitBinaryOperator(bop);
			}
			else if(isa<ReturnStmt>(ifstmt->getThen())){
				ReturnStmt * ret= dyn_cast<ReturnStmt>(ifstmt->getThen());
				this->VisitReturnStmt(ret);	
			}
			else{
                VisitStmt(ifstmt->getThen());
            }
		}
		else{
			if(ifstmt->getElse())
				if(isa<BinaryOperator>(ifstmt->getElse())){
					BinaryOperator * bop = dyn_cast<BinaryOperator>(ifstmt->getElse());
					this->VisitBinaryOperator(bop);
				}
				else if(isa<ReturnStmt>(ifstmt->getElse())){
					ReturnStmt * ret= dyn_cast<ReturnStmt>(ifstmt->getElse());
					this->VisitReturnStmt(ret);
				
				}
				else{
					VisitStmt(ifstmt->getElse());
        		}
    		}
   	}

    virtual void VisitWhileStmt(WhileStmt *whilestmt) {
		#ifdef DEBUG
			std::cout<<"Enter While"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		Expr *expr = whilestmt->getCond();
		Visit(expr);
		bool cond=mEnv->getcond(expr);
		Stmt *body=whilestmt->getBody();
		while(cond){
			if( body && isa<CompoundStmt>(body) ){
				VisitStmt(whilestmt->getBody());
			}
        	//update the condition value
			Visit(expr);
			cond=mEnv->getcond(expr);
      }
   }   

   virtual void VisitForStmt(ForStmt *forstmt ){
	  	#ifdef DEBUG
			std::cout<<"Enter For"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
        Stmt* stmt = forstmt->getInit();
		#ifdef DEBUG
			//std::cout<<stmt<<std::endl;
		#endif
		if(stmt){
			if(isa<BinaryOperator>(stmt)){
				BinaryOperator * bop = dyn_cast<BinaryOperator>(stmt);
				this->VisitBinaryOperator(bop);
			}
       	 	else{
            	VisitStmt(stmt);
			}
		}
        Expr* expr = forstmt->getCond();
        Visit(expr);
        bool cond=mEnv->getcond(expr);
        Stmt* body=forstmt->getBody();
        while(cond){
            if(body && isa<CompoundStmt>(body) ){
                VisitStmt(body);
            }
            Stmt* stmt=forstmt->getInc();
            if(isa<BinaryOperator>(stmt)){
                BinaryOperator* bop = dyn_cast<BinaryOperator>(stmt);
                this->VisitBinaryOperator(bop);
            }
            else{
                VisitStmt(stmt);
            }
            Visit(expr);
            cond=mEnv->getcond(expr);
        }

    }
	
	
	virtual void VisitReturnStmt(ReturnStmt *retstmt){
		#ifdef DEBUG
			std::cout<<"Enter Return"<<std::endl;
		#endif
		if(mEnv->isReturn()) return;
		#ifdef DEBUG
			std::cout<<"enter ret"<<endl;
        #endif
		VisitStmt(retstmt);
		mEnv->ret(retstmt);
		mEnv->setReturn();
	}
	
	virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr* type){
		if(mEnv->isReturn()) return;
		VisitStmt(type);
		mEnv->typetrait(type);
	}
	
	virtual void VisitParenExpr(ParenExpr* paren){
		if(mEnv->isReturn()) return;
		VisitStmt(paren);
		mEnv->paren(paren);
	}
private:
   Environment * mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
   explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
   	   mVisitor(context, &mEnv) {
   }
   virtual ~InterpreterConsumer() {}

   virtual void HandleTranslationUnit(clang::ASTContext &Context) {
	   TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
	   mEnv.init(decl);

	   FunctionDecl * entry = mEnv.getEntry();
	   mVisitor.VisitStmt(entry->getBody());
  }
private:
   Environment mEnv;
   InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public: 
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
  }
};

int main (int argc, char ** argv) {
   if (argc > 1) {
       clang::tooling::runToolOnCode(new InterpreterClassAction, argv[1]);
   }
}

