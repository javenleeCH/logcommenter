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
#include "../logengine/args.h"

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
    // 变量来源：位置模式可命中引用（使用处）或声明（定义处）
    const VarDecl *VD = nullptr;
    if (const auto *Ref = res.Nodes.getNodeAs<DeclRefExpr>("target_ref")) {
      VD = llvm::dyn_cast<VarDecl>(Ref->getDecl());
    }
    if (!VD) VD = res.Nodes.getNodeAs<VarDecl>("target_var");
    if (!VD) return;
    if (args_.line > 0 && !covers(res, VD)) return;

    std::string type = VD->getType().getAsString();
    std::string var = VD->getNameAsString();
    ObsFormat fmt = formatForType(type, var);
    std::string line;
    if (fmt.streamable) {
      line = "std::cout << \"[obs] " + var + "=\" << " + fmt.expr +
             " << std::endl;";
    } else {
      // 对象等不可直接 <<：打印地址兜底，保证有观测输出
      line = "std::cout << \"[obs] " + var + " (ptr)=\" << (const void*)&" +
             var + " << std::endl;";
    }
    SourceLocation end = VD->getSourceRange().getEnd().getLocWithOffset(1);
    col_->inserts.push_back({end, "\n    " + line});
  }

  // 位置过滤：传入的 position (line,col) 必须落在该变量的声明/引用范围上。
  // 命中返回 true；未给 position 时不过滤（此时由 matcher 的 hasName 收敛）。
  bool covers(const MatchFinder::MatchResult &res, const VarDecl *VD) const {
    SourceManager &SM = res.Context->getSourceManager();
    SourceLocation b = SM.getSpellingLoc(VD->getSourceRange().getBegin());
    SourceLocation e = SM.getSpellingLoc(VD->getSourceRange().getEnd());
    unsigned bl = SM.getSpellingLineNumber(b);
    unsigned bcl = SM.getSpellingColumnNumber(b);
    unsigned el = SM.getSpellingLineNumber(e);
    unsigned ecl = SM.getSpellingColumnNumber(e);
    unsigned L = static_cast<unsigned>(args_.line);
    unsigned C = static_cast<unsigned>(args_.col);
    if (L < bl || L > el) return false;
    if (L == bl && C < bcl) return false;
    if (L == el && C > ecl) return false;
    return true;
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
    } else if (args_.action == Action::kInject) {
      if (!args_.var.empty()) {
        // 按变量名：只匹配同名声明
        mf_->addMatcher(varDecl(hasName(args_.var)).bind("target_var"),
                        &observer_);
      } else if (args_.line > 0) {
        // 按位置：匹配引用（使用处）与声明（定义处），回调里按 (line,col) 过滤
        mf_->addMatcher(declRefExpr().bind("target_ref"), &observer_);
        mf_->addMatcher(varDecl().bind("target_var"), &observer_);
      }
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

// 运行时基于可执行文件所在目录定位 clang 内置头：随包分发 <exeDir>/clang-headers。
// resource-dir 设为 exeDir（一定存在，满足 clang 的校验），真正的内置头由 -isystem
// 指向 exeDir/clang-headers。不再依赖编译期 bake 的构建机路径（那在本机不存在）。
std::string findResourceDir(const std::string &exePath, std::string *err) {
  namespace fs = std::filesystem;
  fs::path p(exePath);
  fs::path dir = p.has_parent_path() ? p.parent_path() : fs::path(".");
  // CI/相对启动时 argv[0] 可能是相对路径（如 ./build/logcommenter），必须转成绝对，
  // 否则 clang 对相对 -resource-dir 的解析会随工作目录漂移而找不到内置头。
  std::error_code ec;
  fs::path absDir = fs::absolute(dir, ec);
  if (ec || !fs::exists(absDir, ec)) {
    if (err) *err = "exe dir not found: " + absDir.string();
    return "";
  }
  return absDir.string();
}

}  // namespace

bool runClangEngine(const CliArgs &args, const std::string &compileJson,
                    const std::string &exePath,
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
  // 静态 exe 无系统 clang 安装，clang 内置头（stddef.h 等 builtin 头）只从
  // resource-dir 的 lib/clang/<ver>/include 查找，-isystem 对它们无效。
  // 因此随包分发内置头到 <exeDir>/lib/clang/<CLANG_VERSION_STRING>/include，
  // resource-dir 指向 exeDir（绝对路径），clang 按自身 CLANG_VERSION 匹配版本目录。
  {
    namespace fs = std::filesystem;
    std::string rd = findResourceDir(exePath, err);
    tool.appendArgumentsAdjuster(
        [rd](const CommandLineArguments &args, StringRef filename) {
          // MSVC(CMake) compile_commands 的 command 常自带源文件路径 + /Fo 输出参数，
          // ClangTool 又会注入 --file 的源文件，导致"多个输入 + /Fo 单输出"冲突
          // （cannot specify '/Fo' when compiling multiple source files）。
          // 剥离 command 中与当前源文件重复的 token，只留编译选项。
          std::vector<std::string> vec(args.begin(), args.end());
          std::string cur(filename.data(), filename.size());
          auto cleaned = stripSourceFileArg(vec, cur);
          CommandLineArguments out(cleaned.begin(), cleaned.end());
          if (!rd.empty()) out.push_back("-resource-dir=" + rd);
          // 健壮化：MSVC(CMake) 的 compile_commands command 是 cl.exe / clang-cl。
          // 从编译器路径推导它所属工具链的 include（MSVC STL + Windows SDK ucrt/shared/um），
          // 注入 -isystem —— 不依赖环境 INCLUDE，和 clangd 依据编译器路径找头文件一致。
          // 编译器在 PATH（如 build.bat 的 CC=cl.exe）时 compiler 为相对名，parent_path 为空，
          // 目录存在性检查失败即跳过，安全。
          if (!cleaned.empty()) {
            namespace fs2 = std::filesystem;
            std::string bc = fs2::path(cleaned[0]).filename().string();
            std::string lower;
            for (char c : bc) lower += (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            if (lower == "cl.exe" || lower == "clang-cl.exe" || lower == "clang-cl") {
              fs2::path cp(cleaned[0]);
              fs2::path ver = cp.parent_path().parent_path().parent_path().parent_path();
              fs2::path msvcInc = ver / "include";   // ...\MSVC\<ver>\include (MSVC STL)
              std::error_code ec;
              if (fs2::is_directory(msvcInc, ec)) {
                // clang 处理 MSVC(CMake) command 时是 clang-cl 模式，只认 -imsvc / /external:I，
                // 不认 -isystem（会被忽略）。用 -imsvc 表示 MSVC include。
                out.push_back("-imsvc" + msvcInc.string());
                fs2::path root = ver.parent_path().parent_path().parent_path(); // 工具链根
                fs2::path kits = root / "Windows Kits" / "10" / "Include";
                std::vector<fs2::path> sdkVers;
                ec.clear();
                for (auto it = fs2::directory_iterator(kits, ec);
                     !ec && it != fs2::directory_iterator(); it.increment(ec))
                  if (it->is_directory(ec)) sdkVers.push_back(it->path());
                if (!sdkVers.empty()) {
                  std::sort(sdkVers.begin(), sdkVers.end());
                  fs2::path sdk = sdkVers.back();  // 取最新 SDK 版本
                  for (const char *sub : {"ucrt", "shared", "um"}) {
                    fs2::path d = sdk / sub;
                    if (fs2::is_directory(d, ec)) out.push_back("-imsvc" + d.string());
                  }
                }
              }
            }
          }
          // 调试：LOGCOMMENTER_DEBUG=1 时打印实际 resource-dir、clang 版本与内置头位置
          if (std::getenv("LOGCOMMENTER_DEBUG")) {
            std::string v = CLANG_VERSION_STRING;
            std::string base = rd + "/lib/clang";
            std::error_code ec;
            std::string hdr = base + "/" + v + "/include/stddef.h";
            std::string dirs;
            for (auto it = fs::directory_iterator(base, ec);
                 !ec && it != fs::directory_iterator(); it.increment(ec))
              dirs += it->path().filename().string() + " ";
            std::fprintf(stderr,
                "[logc] resource-dir=%s clang=%s stddef=%s lib/clang=[%s]\n",
                rd.c_str(), v.c_str(), fs::exists(hdr, ec) ? "yes" : "no",
                dirs.c_str());
            out.push_back("-v");  // 打印 clang 实际 include 搜索路径（builtin + GCC）
          }
          return out;
        });
  }
  EngineFactory factory(args, edits, err);
  int rc = tool.run(&factory);
  std::remove(tmpDb.c_str());
  if (rc != 0 && err && err->empty()) {
    *err = "clang tool failed with code " + std::to_string(rc);
  }
  return rc == 0;
}

}  // namespace logc
