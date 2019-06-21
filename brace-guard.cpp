#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Mangle.h"
#include "clang/ARCMigrate/ARCMT.h"
#include "clang/Lex/Lexer.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory BraceGuardCategory("brace-guard options");

class BraceGuardVisitor : public RecursiveASTVisitor<BraceGuardVisitor> {
  private:
    ASTContext *astContext; // used for getting additional AST info
    Rewriter &TheRewriter;
    void dfsTraverse(Stmt *root, int depth)
    {
      if (root == nullptr) 
        return;
      SourceManager &smgr = astContext->getSourceManager();
      for (auto stmtIt = root->child_begin(), stmtEnd = root->child_end(); stmtIt != stmtEnd; ++ stmtIt) {
        Stmt* stmt = *stmtIt;
        if (stmt == nullptr)
          continue;
        braceGuard(stmt);
        dfsTraverse(stmt, depth+1);
      }
    }
    
    // check if the body requires guard or not
    bool requireGuard(Stmt* stmt){
        if(stmt==NULL) printf("no body");
        if(!stmt || isa<CompoundStmt>(stmt))
            return false;
        return true;
    }
    
    size_t findLocationAfter(SourceLocation loc, SourceManager &smgr, char ch, bool before){
       const char* endCharPtr = smgr.getCharacterData(loc);
       size_t offsetSemicolon = 0;
       while((endCharPtr[offsetSemicolon-1]) != ch ){
           offsetSemicolon += before?(-1):(1);
       }
       return offsetSemicolon;
    }

    void insertGurad(Stmt * BodyStmt){
        return insertGurad(BodyStmt, false);
    }

    void insertGurad(Stmt * BodyStmt, bool isElse){
        if(!requireGuard(BodyStmt)) return;
        SourceManager &smgr = astContext->getSourceManager();
        clang::LangOptions lopt;
        SourceLocation StartLoc,EndLoc;
        size_t offset;

        StartLoc = BodyStmt->getLocStart();
        offset = findLocationAfter(StartLoc, smgr, isElse?'e':')', true);
        StartLoc = StartLoc.getLocWithOffset(offset);

        EndLoc = 
            Lexer::getLocForEndOfToken(BodyStmt->getLocEnd(), 0, smgr, lopt);
        offset = findLocationAfter(EndLoc, smgr, ';', false);
        EndLoc = EndLoc.getLocWithOffset(offset);
        
        TheRewriter.InsertTextAfter(EndLoc, StringRef("\n}"));
        TheRewriter.InsertTextAfter(StartLoc, StringRef("{"));
    }

    void braceGuard(Stmt* stmt){
        if (isa<ForStmt>(stmt)){
            auto CastStmt = cast<ForStmt>(stmt);
            Stmt* BodyStmt = CastStmt->getBody();
            insertGurad(BodyStmt); 
        }
        else if (isa<WhileStmt>(stmt)){
            auto CastStmt = cast<WhileStmt>(stmt);
            Stmt* BodyStmt = CastStmt->getBody();
            insertGurad(BodyStmt); 
        }
        else if (isa<IfStmt>(stmt)){
            auto CastStmt = cast<IfStmt>(stmt);
            Stmt* BodyStmt = CastStmt->getThen();
            insertGurad(BodyStmt); 
            Stmt* ElseStmt = CastStmt->getElse();
            if(ElseStmt && !isa<IfStmt>(ElseStmt))
                insertGurad(ElseStmt,true);
        }
        else return;

    }


  public:
    explicit BraceGuardVisitor(CompilerInstance *CI, Rewriter &R) 
      : astContext(&(CI->getASTContext())), TheRewriter(R) // initialize private members
    { 
    }
    
    virtual bool VisitFunctionDecl(FunctionDecl *func) {
      if (!func->hasBody()) 
        return true;
      // only function definitions in current file will be parsed.
      SourceManager &smgr = astContext->getSourceManager();
      if (smgr.getMainFileID() != smgr.getFileID(func->getLocStart()))
        return true;
      if (!func->isThisDeclarationADefinition())
        return true;
      dfsTraverse(func->getBody(), 0);
      return true;
    }
};

class BraceGuardASTConsumer : public ASTConsumer {
  private:
    BraceGuardVisitor *visitor; // doesn't have to be private
  public:
    // override the constructor in order to pass CI
    explicit BraceGuardASTConsumer(CompilerInstance *CI, Rewriter &R)
      : visitor(new BraceGuardVisitor(CI, R)) // initialize the visitor
    { }
    virtual void HandleTranslationUnit(ASTContext &Context) {
      /* we can use ASTContext to get the TranslationUnitDecl, which is
         a single Decl that collectively represents the entire source file */
      visitor->TraverseDecl(Context.getTranslationUnitDecl());
    }
};

class BraceGuardFrontendAction : public ASTFrontendAction {
  public:
    virtual ASTConsumer *CreateASTConsumer(CompilerInstance &CI, StringRef file) {
      filename = file;
      TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
      return new BraceGuardASTConsumer(&CI, TheRewriter); // pass CI pointer to ASTConsumer
    }

    void EndSourceFileAction() override {
      SourceManager &SM = TheRewriter.getSourceMgr();
      llvm::errs() << "** EndSourceFileAction for: " << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";
      // Now emit the rewritten buffer.
      string error_code;
      char fname[200];
      pair<StringRef, StringRef> fname_pair;
      fname_pair = filename.rsplit('.');
      sprintf(fname, "%s_tprof_subs.%s", filename.data(), fname_pair.second.data());
      raw_fd_ostream outFile(fname, error_code, llvm::sys::fs::F_None);
      TheRewriter.getEditBuffer(SM.getMainFileID()).write(outFile);
      outFile.close();
    }

  private:
      Rewriter TheRewriter;
      StringRef filename;
};

int main(int argc, const char **argv) {
  // parse the command-line args passed to your code
  CommonOptionsParser op(argc, argv, BraceGuardCategory);        
  // create a new Clang Tool instance (a LibTooling environment)
  ClangTool Tool(op.getCompilations(), op.getSourcePathList());
  // run the Clang Tool, creating a new FrontendAction (explained below)
  int result = Tool.run(newFrontendActionFactory<BraceGuardFrontendAction>().get());
  return result;
}
