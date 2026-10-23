// Clang 适配层实现：LibTooling 单文件处理，偏移索引精准取编译参数。
#include "engine_clang.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include <llvm/ADT/SmallVector.h>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>   // getpid
#endif

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

// 取 loc 所在行的前导空白（真实缩进，跟随上下文）。
std::string lineIndentAt(const SourceManager &SM, SourceLocation loc) {
  unsigned line = SM.getSpellingLineNumber(loc);
  FileID fid = SM.getFileID(loc);
  SourceLocation ls = SM.translateLineCol(fid, line, 1);
  bool invalid = false;
  const char *data = SM.getCharacterData(ls, &invalid);
  if (invalid || !data) return "";
  std::string ind;
  for (int i = 0; i < 128 && data[i] != '\0'; ++i) {
    char c = data[i];
    if (c == ' ' || c == '\t') ind += c;
    else break;
  }
  return ind;
}

// 从某条语句向上找最近的 enclosing FunctionDecl（全局变量观测点定位）。
const FunctionDecl *enclosingFunction(ASTContext &Ctx, const Stmt *s) {
  llvm::SmallVector<const Stmt *, 16> stack;
  stack.push_back(s);
  while (!stack.empty()) {
    const Stmt *cur = stack.pop_back_val();
    for (auto &p : Ctx.getParents(*cur)) {
      if (const auto *FD = p.get<FunctionDecl>()) return FD;
      if (const auto *st = p.get<Stmt>()) stack.push_back(st);
    }
  }
  return nullptr;
}

// 幂等探测：data 起点之后至多 256 字符 / 两个换行内是否已出现 marker。
bool textHasMarker(const char *data, const std::string &marker) {
  if (!data || marker.empty()) return false;
  int newlines = 0;
  for (int i = 0; i < 256 && data[i] != '\0'; ++i) {
    if (data[i] == '\n' && ++newlines > 2) break;
    if (data[i] == marker[0] &&
        std::strncmp(data + i, marker.c_str(), marker.size()) == 0)
      return true;
  }
  return false;
}

// 收集插入点（SourceLocation + 文本），文件处理结束后统一转 TextEdit。
// inserts   —— 主文件（--file）内的观测日志插入
// friendInserts —— 跨文件（类所在头文件）的 friend operator<< 注入
struct Collector {
  std::vector<std::pair<SourceLocation, std::string>> inserts;
  std::vector<std::pair<SourceLocation, std::string>> friendInserts;
};

class Observer : public MatchFinder::MatchCallback {
 public:
  Observer(const CliArgs &args, Collector *c) : args_(args), col_(c) {}

  void run(const MatchFinder::MatchResult &res) override {
    // 预收集：inject 模式下先收 TU 内可见的全部 operator<< 声明，供类型实查
    if (const auto *FD = res.Nodes.getNodeAs<FunctionDecl>("op_shift")) {
      shiftOps_.push_back(FD);
      return;
    }
    if (args_.action == Action::kComment) {
      handleComment(res);
    } else if (args_.action == Action::kInject) {
      handleInject(res);
    }
  }

 private:
  const CliArgs &args_;
  Collector *col_;
  std::set<const VarDecl *> seen_;  // 已注入变量（canonical decl），position 模式去重
  std::set<unsigned> commentedLines_;  // 已注释行号（同一行多条日志只插一份 "// "）
  std::vector<const FunctionDecl *> shiftOps_;  // TU 内可见的 operator<< 声明

  bool isTarget(const StringLiteral *Str) const {
    return Str->getString().find("pengtest") != StringRef::npos;
  }

  // 判定类型是否是可流式输出目标（operator<< 实查）：遍历 TU 内可见的
  // operator<< 自由函数，要求第一参数是 std::ostream 系、第二参数与本变量
  // 类型兼容（const T&/T 或模板参数）。ADL 重载若在本 TU 不可见则探测不到，
  // 保守降级为不可流式（与 clangd 行为近似：在可见声明范围内决议）。
  static bool isOStreamType(const ASTContext &Ctx, QualType t) {
    t = t.getNonReferenceType().getUnqualifiedType();
    const auto *RT = t->getAs<RecordType>();
    if (!RT) return false;
    std::string n = RT->getDecl()->getNameAsString();
    // std::ostream 是 std::basic_ostream<char, char_traits<char>> 的别名
    return n == "basic_ostream" || n == "ostream";
  }

  bool isStreamableViaOperator(const ASTContext &Ctx,
                               QualType varType) const {
    QualType vt = varType.getNonReferenceType().getUnqualifiedType();
    for (const FunctionDecl *FD : shiftOps_) {
      if (FD->getNumParams() < 2) continue;
      if (!isOStreamType(Ctx, FD->getParamDecl(0)->getType())) continue;
      QualType p1 =
          FD->getParamDecl(1)->getType().getNonReferenceType().getUnqualifiedType();
      // 模板参数（template<typename T>）接受任意类型
      if (p1->isTemplateTypeParmType() || p1->isDependentType()) return true;
      if (Ctx.hasSameType(p1, vt)) return true;
      // 跨 typedef / 命名空间拼写的字符串近似（保守：只在核心类型名相同时命中）
      if (simplifyType(p1.getAsString()) == simplifyType(vt.getAsString()))
        return true;
    }
    return false;
  }

  // 类内是否已有 operator<<（friend 声明或成员）。幂等：已注入过 friend 就不再注入。
  static bool recordHasStreamOperator(const RecordDecl *RD) {
    for (const Decl *D : RD->decls()) {
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (FD->getDeclName().getAsString() == "operator<<") return true;
      }
      if (const auto *Fr = dyn_cast<FriendDecl>(D)) {
        if (const NamedDecl *ND = Fr->getFriendDecl())
          if (ND->getDeclName().getAsString() == "operator<<") return true;
      }
    }
    return false;
  }

  // 生成 inline friend operator<< 定义（MVP：打印类型名 + 对象地址）。
  // 类名取自定义处；非模板、有完整定义的 Record 才支持。
  static std::string friendShiftText(const std::string &tname,
                                     const std::string &indent) {
    std::string in = indent + "  ";
    return "\n" + indent + "  friend std::ostream& operator<<(std::ostream& _os, const " +
           tname + "& _v) {\n" + in + "    _os << \"(" + tname +
           "@\" << static_cast<const void*>(&_v) << \")\";\n" + in +
           "    return _os;\n" + indent + "  }\n" + indent;
  }

  // ---- --comment ----

  void handleComment(const MatchFinder::MatchResult &res) {
    const CallExpr *Call = res.Nodes.getNodeAs<CallExpr>("log_call");
    const StringLiteral *Str =
        res.Nodes.getNodeAs<StringLiteral>("func_log_str");
    if (!Call) {
      Call = res.Nodes.getNodeAs<CallExpr>("log_stream");
      Str = res.Nodes.getNodeAs<StringLiteral>("stream_log_str");
    }
    if (!Call || !Str || !isTarget(Str)) return;
    // 流式日志是链式 operator<<：match 命中的是最内层调用，须逐层上溯取整条
    // 链的范围，否则多行链只注释首行会留下悬空续行、破坏语法。
    const CallExpr *full = Call;
    for (;;) {
      auto P = res.Context->getParents(*full);
      if (P.empty()) break;
      const CallExpr *up = P[0].get<CallExpr>();
      if (!up) break;
      const FunctionDecl *FD = up->getDirectCallee();
      if (!FD || FD->getNameAsString() != "operator<<") break;
      full = up;
    }
    commentStatementLines(res, full->getSourceRange());
  }

  // 对语句范围覆盖的每一行行首插 "// "（可逆：删除 "// " 即还原）。
  // 多行语句（如链式 << 跨行）必须每行都注释，否则续行成为悬空代码。
  void commentStatementLines(const MatchFinder::MatchResult &res,
                             SourceRange R) {
    SourceManager &SM = res.Context->getSourceManager();
    SourceLocation b = SM.getSpellingLoc(R.getBegin());
    SourceLocation e = SM.getSpellingLoc(R.getEnd());
    if (b.isInvalid() || e.isInvalid()) return;
    FileID fid = SM.getFileID(b);
    unsigned bl = SM.getSpellingLineNumber(b);
    unsigned el = SM.getSpellingLineNumber(e);
    for (unsigned L = bl; L <= el; ++L) {
      // 同一行并排多条目标日志只插一份
      if (!commentedLines_.insert(L).second) continue;
      SourceLocation ls = SM.translateLineCol(fid, L, 1);
      if (ls.isInvalid()) continue;
      col_->inserts.push_back({ls, "// "});
    }
  }

  // ---- --inject ----

  void handleInject(const MatchFinder::MatchResult &res) {
    // 变量来源：位置模式可命中引用（使用处）或声明（定义处）
    const DeclRefExpr *Ref = res.Nodes.getNodeAs<DeclRefExpr>("target_ref");
    const VarDecl *VD = Ref ? llvm::dyn_cast<VarDecl>(Ref->getDecl())
                            : nullptr;
    if (!VD) VD = res.Nodes.getNodeAs<VarDecl>("target_var");
    if (!VD) return;
    // 位置模式：光标必须落在命中的节点（声明或引用）自身范围内。
    // 顺序要求：先位置过滤再 seen_ 去重——varDecl 与 declRefExpr 两个 matcher
    // 对同一变量各回调一次，若先去重，"位置在引用处"的命中会被未过位置的
    // 声明命中提前占用 seen_ 而整体落空。
    if (args_.line > 0 &&
        !locInRange(res, Ref ? Ref->getSourceRange() : VD->getSourceRange()))
      return;
    // 变量来源分三类：
    //   local  —— 函数局部变量：声明语句之后插入（原有逻辑）
    //   param  —— 形参：函数体开头插入
    //   global —— 全局变量 / 类静态成员：仅 position 模式 + 引用处命中
    //             （引用发生在函数内）才可确定观测点，插到该函数体开头；
    //             纯 --var 无法确定观测点（全局作用域不能插语句），跳过。
    const bool isParam = isa<ParmVarDecl>(VD);
    const bool isLocal = !isParam && VD->getParentFunctionOrMethod() != nullptr;
    if (!isParam && !isLocal && (args_.line <= 0 || !Ref)) return;
    if (!seen_.insert(VD->getCanonicalDecl()).second) return;

    std::string type = VD->getType().getAsString();
    std::string var = VD->getNameAsString();
    ObsFormat fmt = formatForType(type, var);
    // operator<< 实查：白名单未覆盖的自定义类型，只要 TU 内可见存在可用重载
    // （std::ostream& << T / 模板 <<），同样可直接流式打印，避免误走地址兜底。
    if (!fmt.streamable &&
        isStreamableViaOperator(*res.Context, VD->getType())) {
      fmt.streamable = true;
      fmt.expr = var;
    }
    // friend 注入（需用户显式 --friend）：对象不可流式且类型为用户可改的
    // 非模板类（有完整定义）时，在类 '}' 前注入 inline friend operator<<，
    // 观测表达式改为流式。改的是类所在头文件（跨文件 TextEdit），有副作用，
    // 故必须显式确认。类内已有 operator<<（friend 或成员）时跳过（幂等）。
    if (!fmt.streamable && args_.friend_) {
      QualType qt = VD->getType().getNonReferenceType().getUnqualifiedType();
      const auto *RT = qt->getAs<RecordType>();
      const RecordDecl *RD = RT ? RT->getDecl()->getDefinition() : nullptr;
      if (RD && RD->getBraceRange().isValid() && RD->getIdentifier() &&
          !RD->isTemplated() && !recordHasStreamOperator(RD)) {
        SourceLocation be = RD->getBraceRange().getEnd();
        SourceManager &SM = res.Context->getSourceManager();
        std::string indent = lineIndentAt(SM, be);
        col_->friendInserts.push_back(
            {be, friendShiftText(RD->getNameAsString(), indent)});
        fmt.streamable = true;
        fmt.expr = var;  // 与 operator<< 实查分支一致：观测行 << 表达式必须是变量
      }
    }
    std::string line;
    std::string marker;  // 已注入幂等标记（见 alreadyObserved）
    if (fmt.streamable) {
      line = "std::cout << \"[obs] " + var + "=\" << " + fmt.expr +
             " << std::endl;";
      marker = "\"[obs] " + var + "=\"";
    } else {
      // 对象等不可直接 <<：打印地址兜底，保证有观测输出
      line = "std::cout << \"[obs] " + var + " (ptr)=\" << (const void*)&" +
             var + " << std::endl;";
      marker = "\"[obs] " + var + " (ptr)=\"";
    }
    // 插入点按来源分派：
    //   local  —— 声明语句（含 ';'）之后（旧逻辑，已修过"把 42 劈两半"缺陷）；
    //   param / global —— 所在函数体 { 之后（形参在定义处、全局在引用所在函数）。
    SourceLocation at;
    if (isLocal) {
      at = insertLocAfterDecl(res, VD);
    } else if (isParam) {
      at = insertLocAtFunctionBodyStart(
          res, dyn_cast<FunctionDecl>(VD->getParentFunctionOrMethod()));
    } else {
      at = insertLocAtFunctionBodyStart(res, enclosingFunction(*res.Context, Ref));
    }
    if (at.isInvalid()) return;
    // 幂等（方案 §3.4）：插入点后两行内已有该变量的 [obs] 观测行则跳过，
    // 重复调用不叠加。
    if (alreadyObserved(res, at, marker)) return;
    // 缩进跟随上下文：新日志行与插入点所在行（声明行 / 函数体 { 行）同缩进。
    std::string indent = lineIndentAt(res.Context->getSourceManager(), at);
    col_->inserts.push_back({at, "\n" + indent + line});
  }

  // 插入点：优先声明语句结尾（DeclStmt，含 ';'）之后；for/if/switch/while
  // 头部声明的观测日志插到复合语句体开头（插在头部内会破坏语法）；体不是
  // 复合语句（无大括号）时放弃——插入会把原语句挤出循环/分支作用域。
  SourceLocation insertLocAfterDecl(const MatchFinder::MatchResult &res,
                                    const VarDecl *VD) const {
    SourceManager &SM = res.Context->getSourceManager();
    const LangOptions &LO = res.Context->getLangOpts();
    auto parents = res.Context->getParents(*VD);
    const DeclStmt *DS = parents.empty() ? nullptr : parents[0].get<DeclStmt>();
    if (DS) {
      const Stmt *body = nullptr;
      auto gp = res.Context->getParents(*DS);
      if (!gp.empty()) {
        if (const auto *F = gp[0].get<ForStmt>())
          body = F->getBody();
        else if (const auto *I = gp[0].get<IfStmt>())
          body = I->getThen();
        else if (const auto *S = gp[0].get<SwitchStmt>())
          body = S->getBody();
        else if (const auto *W = gp[0].get<WhileStmt>())
          body = W->getBody();
      }
      if (body) {
        if (!isa<CompoundStmt>(body)) return SourceLocation();
        return Lexer::getLocForEndOfToken(body->getBeginLoc(), 0, SM, LO);
      }
      return Lexer::getLocForEndOfToken(DS->getEndLoc(), 0, SM, LO);
    }
    // 无 DeclStmt（理论上函数局部都有）：退回声明末 token 之后
    return VD->getSourceRange().getEnd().getLocWithOffset(1);
  }

  // 插入点：函数体 { 之后（形参 / 全局变量观测）。仅函数**定义**（有复合体）
  // 支持；函数声明（无体）放弃——插进去会破坏语法。
  SourceLocation insertLocAtFunctionBodyStart(const MatchFinder::MatchResult &res,
                                              const FunctionDecl *FD) const {
    if (!FD) return SourceLocation();
    const Stmt *bodyStmt = FD->getBody();
    const auto *body = bodyStmt ? dyn_cast<CompoundStmt>(bodyStmt) : nullptr;
    if (!body) return SourceLocation();
    SourceManager &SM = res.Context->getSourceManager();
    const LangOptions &LO = res.Context->getLangOpts();
    return Lexer::getLocForEndOfToken(body->getBeginLoc(), 0, SM, LO);
  }

  // 幂等探测：插入点后的源码（至多两行）已含该变量的观测标记则视为已注入。
  bool alreadyObserved(const MatchFinder::MatchResult &res,
                       SourceLocation at, const std::string &marker) const {
    if (!at.isValid() || marker.empty()) return false;
    SourceManager &SM = res.Context->getSourceManager();
    bool invalid = false;
    const char *data = SM.getCharacterData(at, &invalid);
    if (invalid || !data) return false;
    return textHasMarker(data, marker);
  }

  // 位置过滤：传入的 position (line,col) 必须落在范围上（含边界）。
  // 命中返回 true；未给 position 时不过滤（此时由 matcher 的 hasName 收敛）。
  bool locInRange(const MatchFinder::MatchResult &res, SourceRange R) const {
    SourceManager &SM = res.Context->getSourceManager();
    const LangOptions &LO = res.Context->getLangOpts();
    SourceLocation b = SM.getSpellingLoc(R.getBegin());
    SourceLocation e = SM.getSpellingLoc(R.getEnd());
    if (b.isInvalid() || e.isInvalid()) return false;
    unsigned bl = SM.getSpellingLineNumber(b);
    unsigned bcl = SM.getSpellingColumnNumber(b);
    unsigned el = SM.getSpellingLineNumber(e);
    unsigned ecl = SM.getSpellingColumnNumber(e);
    // clang 的 SourceRange::getEnd() 指向末 token **起点**：单 token 范围
    // （如 DeclRefExpr 的变量名）只有首字符能命中。扩到末 token 结束处，
    // 使"光标落在 token 任意位置"都算命中。
    SourceLocation e2 = Lexer::getLocForEndOfToken(e, 0, SM, LO);
    if (e2.isValid()) {
      el = SM.getSpellingLineNumber(e2);
      ecl = SM.getSpellingColumnNumber(e2) - 1;
    }
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
      // 预收集 TU 内可见的 operator<< 声明（inject 类型实查用）。
      // 收集回调在 run() 中先于 handleInject 处理（见 Observer::run）。
      mf_->addMatcher(
          functionDecl(hasOverloadedOperatorName("<<")).bind("op_shift"),
          &observer_);
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
    for (auto &p : col_.inserts) {
      TextEdit e;
      e.start.line = SM.getSpellingLineNumber(p.first);
      e.start.col = SM.getSpellingColumnNumber(p.first);
      e.end = e.start;
      e.text = p.second;
      edits_->push_back(e);
    }
    // friend 注入：目标在类所在文件（通常为头文件，跨文件 TextEdit）。
    // 类定义在主文件（--file）时用 --file 路径，否则用 SourceManager 解析
    // 出的头文件路径。
    for (auto &p : col_.friendInserts) {
      FileID fid = SM.getFileID(p.first);
      TextEdit e;
      e.start.line = SM.getSpellingLineNumber(p.first);
      e.start.col = SM.getSpellingColumnNumber(p.first);
      e.end = e.start;
      e.text = p.second;
      if (fid == SM.getMainFileID()) {
        e.file = args_.file;
      } else {
        std::string f = SM.getFilename(p.first).str();
        e.file = f.empty() ? args_.file : f;
      }
      edits_->push_back(e);
    }
    // 整行模式（产品方案 §3.4：选中整行插入该行前）：--position 未落在任何
    // 变量 token 上（无注入）且未指定 --var 时，在选中行行首插入一行位置
    // 标记日志，缩进跟随原行；空行 / 该行已注入过则跳过。
    if (edits_->empty() && args_.action == Action::kInject &&
        args_.line > 0 && args_.var.empty()) {
      const unsigned L = static_cast<unsigned>(args_.line);
      SourceLocation ls = SM.translateLineCol(SM.getMainFileID(), L, 1);
      if (ls.isValid()) {
        bool invalid = false;
        const char *data = SM.getCharacterData(ls, &invalid);
        if (!invalid && data && *data != '\0' && *data != '\n' && *data != '\r') {
          std::string marker = "\"[obs] line " + std::to_string(L) + "\"";
          if (!textHasMarker(data, marker)) {
            TextEdit e;
            e.start.line = static_cast<int>(L);
            e.start.col = 1;
            e.end = e.start;
            e.text = lineIndentAt(SM, ls) +
                     "std::cout << \"[obs] line " + std::to_string(L) +
                     "\" << std::endl;\n";
            edits_->push_back(e);
          }
        }
      }
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

// 把单条条目写为临时 compile_commands.json，避免 loadFromFile 全量解析 1GB。
// 不用 std::tmpnam：可预测文件名有竞态与符号链接攻击面（且 MSVC C4996），
// 改用 temp 目录 + 进程 PID + 进程内序号构造唯一名。
std::string writeSingleEntryDb(const std::string &entryJson) {
  static std::atomic<unsigned> seq{0};
#ifdef _WIN32
  const unsigned pid = static_cast<unsigned>(_getpid());
#else
  const unsigned pid = static_cast<unsigned>(getpid());
#endif
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
  if (ec) dir = ".";
  std::filesystem::path p = dir / ("logc_cc_" + std::to_string(pid) + "_" +
                                   std::to_string(seq++) + ".json");
  std::string tmp = p.string();
  std::FILE *f = std::fopen(tmp.c_str(), "wb");
  if (!f) return {};
  std::fprintf(f, "[\n%s\n]\n", entryJson.c_str());
  std::fclose(f);
  return tmp;
}

// clang 的 resource-dir 语义：直接包含 include/ 的目录（标准布局
// <prefix>/lib/clang/<version>）；内置头（stddef.h 等）只从 <resource-dir>/include
// 查找，-isystem 对它们无效。因此 -resource-dir 必须指到版本目录一级，不能只给 exeDir。
// 探测顺序：
//   1. <exeDir>/lib/clang/<ver>        随包分发（release zip / CI 复制到 exe 旁）
//   2. <LLVM 构建树>/lib/clang/<ver>   编译期 bake（clang-resource-headers 产出）
//   3. <exeDir>                        兜底（目录存在，满足 clang 校验，但内置头缺失）
// 版本目录名可能是完整版本（23.1.3，打包脚本）或仅 major（23，LLVM 构建树），
// 优先精确匹配 CLANG_VERSION_STRING，否则接受任何含 include/stddef.h 的版本目录。
std::string probeResourceDirUnder(const std::filesystem::path &base) {
  namespace fs = std::filesystem;
  fs::path root = base / "lib" / "clang";
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return "";
  fs::path exact = root / CLANG_VERSION_STRING;
  if (fs::exists(exact / "include" / "stddef.h", ec)) return exact.string();
  for (auto it = fs::directory_iterator(root, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec))
    if (it->is_directory(ec) && fs::exists(it->path() / "include" / "stddef.h", ec))
      return it->path().string();
  return "";
}

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
  std::string rd = probeResourceDirUnder(absDir);
  if (rd.empty()) rd = probeResourceDirUnder(fs::path(LOGC_LLVM_BIN_DIR));
  if (rd.empty()) rd = absDir.string();
  return rd;
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
  if (tmpDb.empty()) {
    if (err) *err = "cannot create temp compile db";
    return false;
  }
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
  // 静态链接的 clang 无系统 resource-dir（编译期默认路径只在构建机上存在）。
  // 内置头（stddef.h 等）只从 <resource-dir>/include 查找，-isystem 对它们无效，
  // 因此 -resource-dir 必须指向直接包含 include/ 的版本目录（见 findResourceDir）。
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
          // 剥离写文件类参数（真实 monorepo 常见）：-MMD/-MD/-MF/-MT/-MQ/-o/
          // -serialize-diagnostic-file 等。LibTooling 只做前端解析、不产出
          // .o/.d，保留会让 clang 前端尝试写 obj/... 依赖文件（本机 obj 目录
          // 往往不存在）而报错。只删参数本身与紧随的值，不动其余编译选项。
          for (size_t i = 0; i < cleaned.size();) {
            const std::string &a = cleaned[i];
            if (a == "-MMD" || a == "-MD" || a == "-MG") {
              cleaned.erase(cleaned.begin() + static_cast<long>(i));
              continue;
            }
            if (a == "-MF" || a == "-MT" || a == "-MQ" || a == "-o" ||
                a == "-serialize-diagnostic-file") {
              if (i + 1 < cleaned.size())
                cleaned.erase(cleaned.begin() + static_cast<long>(i),
                              cleaned.begin() + static_cast<long>(i) + 2);
              else
                cleaned.erase(cleaned.begin() + static_cast<long>(i));
              continue;
            }
            ++i;
          }
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
          // 调试：LOGCOMMENTER_DEBUG=1 时打印实际 resource-dir 与内置头命中情况
          if (std::getenv("LOGCOMMENTER_DEBUG")) {
            std::error_code ec;
            std::string hdr = rd + "/include/stddef.h";
            std::fprintf(stderr, "[logc] resource-dir=%s clang=%s stddef=%s\n",
                         rd.c_str(), CLANG_VERSION_STRING,
                         fs::exists(hdr, ec) ? "yes" : "no");
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
