#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Mangle.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

//#define TPROF_DEBUG
#define TPROF_LOGE(fmt, ...)\
  fprintf(stderr, ANSI_COLOR_RED fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define TPROF_LOGW(fmt, ...)\
  fprintf(stderr, ANSI_COLOR_YELLOW fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#ifdef TPROF_DEBUG
#define TPROF_LOGD(fmt, ...)\
  fprintf(stderr, ANSI_COLOR_MAGENTA fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#else
#define TPROF_LOGD(fmt, ...)
#endif
#define TPROF_LOGI(fmt, ...)\
  fprintf(stderr, ANSI_COLOR_BLUE fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define TPROF_LOGS(fmt, ...)\
  fprintf(stderr, ANSI_COLOR_GREEN fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define TPROF_LOGN(fmt, ...)\
  fprintf(stderr, ANSI_COLOR_RESET fmt ANSI_COLOR_RESET, ##__VA_ARGS__)
#define TPROF_LINE TPROF_LOGN("LINE %d\n", __LINE__);

static cl::OptionCategory StructLocatorCategory("struct-locator options");

class StructLocatorVisitor : public RecursiveASTVisitor<StructLocatorVisitor> {
  private:
    ASTContext *astContext; // used for getting additional AST info
    MangleContext *mangleContext;
    const char* cst_dir;
    FILE *fcst;
    void dfsTraverse(Stmt *root, int depth)
    {
      if (root == nullptr) 
        return;
      //TPROF_LOGN("DFS enter level %d\n", depth);
      SourceManager &smgr = astContext->getSourceManager();
      for (auto stmtIt = root->child_begin(), stmtEnd = root->child_end(); stmtIt != stmtEnd; ++ stmtIt) {
        Stmt* stmt = *stmtIt;
        if (stmt == nullptr)
          continue;
        int sloc = smgr.getPresumedLineNumber(stmt->getLocStart());
        int eloc = smgr.getPresumedLineNumber(stmt->getLocEnd());
        //astContext->getSourceManager().getPresumedColumnNumber(Loc));
        if (isa<WhileStmt>(stmt) or isa<ForStmt>(stmt) or isa<DoStmt>(stmt)) {
          //TPROF_LOGN("hit loop, depth = %d\n", depth);
          fprintf(fcst, "LOOP LOOP %d %d\n", sloc, eloc);
          //TPROF_LOGN("LOOP LOOP %d %d\n", sloc, eloc);
        }
        else if (CallExpr *call = dyn_cast<CallExpr>(stmt)) {
          if (FunctionDecl *fd = call->getDirectCallee()) {
            //TPROF_LOGN("hit call, depth = %d\n", depth);
            fprintf(fcst, "CALL %s %d %d\n", fd->getNameInfo().getName().getAsString().c_str(), sloc, eloc);
            //TPROF_LOGN("CALL %s %d %d\n", fd->getNameInfo().getName().getAsString().c_str(), sloc, eloc);
          }
        }
        dfsTraverse(stmt, depth+1);
      }
      //TPROF_LOGN("DFS leave level %d\n", depth);
    }

    string getMangledName(FunctionDecl* decl) {
//#if 1
    // shouldMangleDeclName returns true for decls that aren't/can't be mangled.
      if (!mangleContext->shouldMangleDeclName(decl)) {
        return decl->getNameInfo().getName().getAsString();
      }
// #endif
      std::string mangledName;
      llvm::raw_string_ostream ostream(mangledName);
      mangleContext->mangleCXXName(decl, ostream);
      ostream.flush();
      return mangledName;
    };


  public:
    explicit StructLocatorVisitor(CompilerInstance *CI) 
      : astContext(&(CI->getASTContext())) // initialize private members
    { 
      cst_dir = getenv("TPROF_WORKSPACE");
      if (!cst_dir) {
        TPROF_LOGW("ENV-VAR TPROF_WORKSPACE IS NOT SET%s\n","");
        cst_dir = getenv("PWD");
        TPROF_LOGW("USE $PWD=%s INSTEAD\n", cst_dir);
      }
      else {
        TPROF_LOGD("TPROF_WORKSPACE SET TO %s\n", cst_dir);
      }
      mangleContext = astContext->createMangleContext();
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
      string funcName = getMangledName(func);
      //string funcName = func->getNameInfo().getName().getAsString();
      //errs() << "Visit Function " << funcName << "\n";
      // get location of this function
      // char* location = smgr.getFilename(func->getLocation()).data();
      char cst_fname[200];
      // char location_name[200];
      // strcpy(location_name, location);
      sprintf(cst_fname, "%s/%s.cst", cst_dir, funcName.c_str());
      fcst = fopen(cst_fname, "w");
      if (!fcst) {
        TPROF_LOGE("CANNOT WRITE TO %s\n", cst_fname);
        exit(-1);
      }
      fprintf(fcst, "%s\n", funcName.c_str());
      fprintf(fcst, "%s\n", smgr.getFileEntryForID(smgr.getMainFileID())->getName());
      dfsTraverse(func->getBody(), 0);
      fprintf(fcst, "\n");
      fclose(fcst);
      return true;
    }
};

class StructLocatorASTConsumer : public ASTConsumer {
  private:
    StructLocatorVisitor *visitor; // doesn't have to be private
  public:
    // override the constructor in order to pass CI
    explicit StructLocatorASTConsumer(CompilerInstance *CI)
      : visitor(new StructLocatorVisitor(CI)) // initialize the visitor
    { }
    virtual void HandleTranslationUnit(ASTContext &Context) {
      /* we can use ASTContext to get the TranslationUnitDecl, which is
         a single Decl that collectively represents the entire source file */
      visitor->TraverseDecl(Context.getTranslationUnitDecl());
    }
};

class StructLocatorFrontendAction : public ASTFrontendAction {
  public:
    virtual ASTConsumer *CreateASTConsumer(CompilerInstance &CI, StringRef file) {
      return new StructLocatorASTConsumer(&CI); // pass CI pointer to ASTConsumer
    }
};

int main(int argc, const char **argv) {
  // parse the command-line args passed to your code
  CommonOptionsParser op(argc, argv, StructLocatorCategory);        
  // create a new Clang Tool instance (a LibTooling environment)
  ClangTool Tool(op.getCompilations(), op.getSourcePathList());
  // run the Clang Tool, creating a new FrontendAction (explained below)
  int result = Tool.run(newFrontendActionFactory<StructLocatorFrontendAction>().get());
  return result;
}
