// Clang 适配层实现：LibTooling 单文件处理，偏移索引精准取编译参数。
#include "engine_clang.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <filesystem>

#include "../logengine/compile_index.h"
#include "../logengine/format_table.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

namespace logc {
namespace {

// 收集插入点（SourceLocation + 文本），文件处理结束后统一转 TextEdit
struct Collector {
  std::vector<std::pair<SourceLocation, std::string>> inserts;
};

class Observer : public MatchFinder::MatchCallback {
 public:
  Observer(const CliArgs &args, Collector *c) : args_(args), col_(c) {}

  void run(const MatchFinder::MatchResult &res) override {
    if (args_.action == Action::kComment) {
      handleComment(res);
    } else if (args_.action == Action::kInject) {
      handleInject(res);
    }
  }

 private:
  const CliArgs &args_;
  Collector *col_;

  bool isTarget(const StringLiteral *Str) const {
    return Str->getString().find("pengtest") != StringRef::npos;
  }

  void handleComment(const MatchFinder::MatchResult &res) {
    if (const auto *Str =
            res.Nodes.getNodeAs<StringLiteral>("func_log_str")) {
      if (isTarget(Str)) {
        if (const auto *Call = res.Nodes.getNodeAs<CallExpr>("log_call")) {
          col_->inserts.push_back({Call->getBeginLoc(), "// "});
        }
      }
    }
    if (const auto *Str =
            res.Nodes.getNodeAs<StringLiteral>("stream_log_str")) {
      if (isTarget(Str)) {
        if (const auto *C = res.Nodes.getNodeAs<CallExpr>("log_stream")) {
          col_->inserts.push_back({C->getBeginLoc(), "// "});
        }
      }
    }
  }

  void handleInject(const MatchFinder::MatchResult &res) {
    if (const auto *VD = res.Nodes.getNodeAs<VarDecl>("target_var")) {
      std::string type = VD->getType().getAsString();
      std::string var = VD->getNameAsString();
      ObsFormat fmt = formatForType(type, var);
      if (!fmt.streamable) return;  // 对象等交由后续增强处理
      std::string line =
          "std::cout << \"[obs] " + var + "=\" << " + fmt.expr +
          " << std::endl;";
      SourceLocation end =
          VD->getSourceRange().getEnd().getLocWithOffset(1);
      col_->inserts.push_back({end, "\n    " + line});
    }
  }
};

class EngineConsumer : public ASTConsumer {
 public:
  EngineConsumer(const CliArgs &args, Collector *c, MatchFinder *mf)
      : args_(args), observer_(args, c), mf_(mf) {
    if (args_.action == Action::kComment) {
      mf_->addMatcher(
          callExpr(hasAnyArgument(stringLiteral().bind("func_log_str")))
              .bind("log_call"),
          &observer_);
      mf_->addMatcher(
          callExpr(
              callee(functionDecl(hasName("operator<<"))),
              hasArgument(1, stringLiteral().bind("stream_log_str")),
              hasArgument(0, declRefExpr(
                                 to(varDecl(hasAnyName(
                                     "cout", "cerr", "clog"))))))
              .bind("log_stream"),
          &observer_);
    } else if (args_.action == Action::kInject && !args_.var.empty()) {
      mf_->addMatcher(varDecl(hasName(args_.var)).bind("target_var"),
                      &observer_);
    }
  }
  void HandleTranslationUnit(ASTContext &Ctx) override {
    mf_->matchAST(Ctx);
  }

 private:
  const CliArgs &args_;
  Observer observer_;
  MatchFinder *mf_;
};

class EngineAction : public ASTFrontendAction {
 public:
  EngineAction(const CliArgs &args, std::vector<TextEdit> *edits,
               std::string *err)
      : args_(args), edits_(edits), err_(err) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef) override {
    return std::make_unique<EngineConsumer>(args_, &col_, &mf_);
  }

  void EndSourceFileAction() override {
    SourceManager &SM = getCompilerInstance().getSourceManager();
    const LangOptions &LO = getCompilerInstance().getLangOpts();
    (void)LO;
    for (auto &p : col_.inserts) {
      TextEdit e;
      e.start.line = SM.getSpellingLineNumber(p.first);
      e.start.col = SM.getSpellingColumnNumber(p.first);
      e.end = e.start;
      e.text = p.second;
      edits_->push_back(e);
    }
    if (edits_->empty() && err_) {
      *err_ = "no matching target found";
    }
  }

 private:
  const CliArgs &args_;
  std::vector<TextEdit> *edits_;
  std::string *err_;
  MatchFinder mf_;
  Collector col_;
};

struct EngineFactory : FrontendActionFactory {
  EngineFactory(const CliArgs &a, std::vector<TextEdit> *e, std::string *err)
      : args_(a), edits_(e), err_(err) {}
  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<EngineAction>(args_, edits_, err_);
  }
  const CliArgs &args_;
  std::vector<TextEdit> *edits_;
  std::string *err_;
};

// 把单条条目写为临时 compile_commands.json，避免 loadFromFile 全量解析 1GB
std::string writeSingleEntryDb(const std::string &entryJson) {
  char buf[L_tmpnam];
  const char *name = std::tmpnam(buf);
  std::string tmp = name ? std::string(name) + ".json" : "/tmp/logc_db.json";
  std::FILE *f = std::fopen(tmp.c_str(), "w");
  if (f) {
    std::fprintf(f, "[\n%s\n]\n", entryJson.c_str());
    std::fclose(f);
  }
  return tmp;
}

// 静态链接的自定义可执行无法从 argv[0] 推导 clang resource dir（内置头所在），
// 这里在 LLVM 构建目录下探测真实路径：<bin>/lib/clang/<ver>/include/stddef.h。
// 找不到返回空串，并把诊断信息写入 err。
std::string findResourceDir(std::string *err) {
  namespace fs = std::filesystem;
#ifdef LOGC_LLVM_BIN_DIR
  fs::path lib(LOGC_LLVM_BIN_DIR);
  lib /= "lib";
  lib /= "clang";
  std::error_code ec;
  if (fs::exists(lib)) {
    for (auto &e : fs::directory_iterator(lib, ec)) {
      if (!e.is_directory(ec)) continue;
      fs::path inc = e.path() / "include" / "stddef.h";
      if (fs::exists(inc)) {
        return e.path().string();  // <bin>/lib/clang/<ver>
      }
    }
    if (err) *err = "no stddef.h under resource dir candidates: " + lib.string();
  } else if (err) {
    *err = "resource dir not found: " + lib.string();
  }
#else
  if (err) *err = "LOGC_LLVM_BIN_DIR not defined";
#endif
  return "";
}

}  // namespace

bool runClangEngine(const CliArgs &args, const std::string &compileJson,
                    std::vector<TextEdit> *edits, std::string *err) {
  if (args.file.empty()) {
    if (err) *err = "missing --file";
    return false;
  }
  CompileIndex idx;
  // 优先用持久化索引缓存（mtime 未变则跳过重扫 1GB）
  std::string cache = compileJson + ".logc-idx";
  if (!idx.loadCache(cache, compileJson) || idx.isStale()) {
    if (!idx.build(compileJson, err)) return false;
    idx.saveCache(cache);
  }
  std::string entry;
  if (!idx.readEntry(args.file, &entry)) {
    if (err) *err = "file not found in compile_commands: " + args.file;
    return false;
  }
  std::string tmpDb = writeSingleEntryDb(entry);
  std::string loadErr;
  std::unique_ptr<CompilationDatabase> db =
      JSONCompilationDatabase::loadFromFile(tmpDb, loadErr,
                                            JSONCommandLineSyntax::AutoDetect);
  if (!db) {
    std::remove(tmpDb.c_str());
    if (err) *err = "bad compile db: " + loadErr;
    return false;
  }
  std::vector<std::string> sources = {args.file};
  ClangTool tool(*db, sources);
#ifdef LOGC_LLVM_BIN_DIR
  // 静态链接的自定义可执行无法从 argv[0] 推导 resource dir，必须显式指定，
  // 否则 ClangTool 预处理时找不到 clang 内置头（stddef.h 等）。
  {
    std::string rd = findResourceDir(err);
    std::fprintf(stderr, "[logc] LOGC_LLVM_BIN_DIR=%s\n", LOGC_LLVM_BIN_DIR);
    std::fprintf(stderr, "[logc] resource-dir resolved: '%s' (diag: %s)\n",
                 rd.c_str(), (err && !err->empty()) ? err->c_str() : "");
    if (!rd.empty()) {
      tool.appendArgumentsAdjuster(
          [rd](const CommandLineArguments &args, StringRef) {
            CommandLineArguments out = args;
            out.push_back("-resource-dir=" + rd);
            return out;
          });
    }
  }
#endif
  EngineFactory factory(args, edits, err);
  int rc = tool.run(&factory);
  std::remove(tmpDb.c_str());
  if (rc != 0 && err && err->empty()) {
    *err = "clang tool failed with code " + std::to_string(rc);
  }
  return rc == 0;
}

}  // namespace logc
