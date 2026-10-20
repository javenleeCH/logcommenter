#include <iostream>
#include <memory>
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// 命名空间：只引入必要的类，避免冲突
using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm::cl; // llvm::cl 是命名空间，提供 OptionCategory 等
using std::make_unique;   // 显式引入 std::make_unique
using llvm::errs;
using llvm::outs;
using llvm::Expected;
using llvm::handleAllErrors;
using llvm::ErrorInfoBase;
using llvm::StringRef;

static OptionCategory LogCommenterCategory("log-commenter options");

// 命中含标记字符串（默认 pengtest）的日志语句后，在其前插入 "// " 注释掉
class LogCommenter : public MatchFinder::MatchCallback {
public:
  explicit LogCommenter(Rewriter &R) : Rewrite(R) {}

  void run(const MatchFinder::MatchResult &Result) override {
    // 函数调用型日志：log("pengtest ...")
    if (const auto *Str = Result.Nodes.getNodeAs<clang::StringLiteral>("func_log_str")) {
      if (isTargetString(Str)) {
        if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("log_call")) {
          annotateStatement(Call->getSourceRange(), Result.Context);
        }
      }
    }
    // 流式输出日志：std::cout << "pengtest ..."
    if (const auto *Str = Result.Nodes.getNodeAs<clang::StringLiteral>("stream_log_str")) {
      if (isTargetString(Str)) {
        if (const auto *StreamCall = Result.Nodes.getNodeAs<CallExpr>("log_stream")) {
          annotateStatement(StreamCall->getSourceRange(), Result.Context);
        }
      }
    }
  }

private:
  Rewriter &Rewrite;

  // 是否为要清理的标记日志（改这里换成自己的标记）
  bool isTargetString(const clang::StringLiteral *Str) {
    StringRef Content = Str->getString();
    return Content.find("pengtest") != StringRef::npos;
  }

  void annotateStatement(SourceRange Range, ASTContext *Context) {
    SourceManager &SM = Context->getSourceManager();
    const LangOptions &LangOpts = Context->getLangOpts();

    // 跳过系统头文件与已注释语句，避免重复注释
    if (!Range.isValid() || SM.isInSystemHeader(Range.getBegin())) {
      return;
    }
    CharSourceRange StartRange = CharSourceRange::getCharRange(
        Range.getBegin(),
        Range.getBegin().getLocWithOffset(2)
    );
    StringRef Code = Lexer::getSourceText(StartRange, SM, LangOpts);
    if (Code.starts_with("//")) {
      return;
    }

    Rewrite.InsertText(Range.getBegin(), "// ");
    errs() << "注释了包含 'pengtest' 的语句: "
           << Lexer::getSourceText(CharSourceRange::getTokenRange(Range), SM, LangOpts)
           << "\n";
  }
};

class LogCommenterAction : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef File) override {
    Rewrite = make_unique<Rewriter>(CI.getSourceManager(), CI.getLangOpts());
    Commenter = make_unique<LogCommenter>(*Rewrite);

    // 匹配器：函数调用参数中的字符串字面量
    Finder.addMatcher(
        callExpr(
            hasAnyArgument(stringLiteral().bind("func_log_str"))
        ).bind("log_call"),
        Commenter.get()
    );
    // 匹配器：cout/cerr/clog 的 operator<< 流式输出
    Finder.addMatcher(
        callExpr(
            callee(functionDecl(hasName("operator<<"))),
            hasArgument(1, stringLiteral().bind("stream_log_str")),
            hasArgument(0, declRefExpr(to(varDecl(hasAnyName("cout", "cerr", "clog")))))
        ).bind("log_stream"),
        Commenter.get()
    );

    return Finder.newASTConsumer();
  }

  void EndSourceFileAction() override {
    if (Rewrite && Rewrite->overwriteChangedFiles()) {
      outs() << "已更新文件: " << getCurrentFile() << "\n";
    }
  }

private:
  std::unique_ptr<Rewriter> Rewrite;
  std::unique_ptr<LogCommenter> Commenter;
  MatchFinder Finder;
};

int main(int argc, const char **argv) {
  Expected<CommonOptionsParser> OptionsParser =
      CommonOptionsParser::create(argc, argv, LogCommenterCategory);
  if (!OptionsParser) {
    handleAllErrors(OptionsParser.takeError(), [](const ErrorInfoBase &E) {
      errs() << "命令行参数解析失败: " << E.message() << "\n";
    });
    return 1;
  }

  auto pathList = OptionsParser->getSourcePathList();
  auto &compilationList = OptionsParser->getCompilations();
  std::cout << "处理的源文件列表:\n";
  ClangTool Tool(compilationList, pathList);
  int Result = Tool.run(newFrontendActionFactory<LogCommenterAction>().get());
  if (Result != 0) {
    errs() << "工具运行失败\n";
    return Result;
  }
  return 0;
}
