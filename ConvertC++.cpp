﻿#include "ConvertC++.h"
using namespace std;
using namespace clang;
using namespace clang::tooling;

typedef list<char*> LISTVARS;
LISTVARS listVars;
extern "C" __declspec(dllexport) std::string parserExpr(Stmt * stmt, ASTContext * Context);
extern "C" __declspec(dllexport) void dealInitValue(const Expr * initExpr, std::string * initValueStr);
extern "C" __declspec(dllexport) std::string dealType(const Type * type, std::string typeStr, QualType nodeType, std::string * dimStr);
extern "C" __declspec(dllexport) bool judgeIsParserFile(SourceLocation sourceLoc, ASTContext * Context);
int funArrayLen = 0;
bool isParserIfStmt = false;

std::vector<int> fileLocVector;
std::vector<AllHaveFunBodyHpp> hppFuns;
std::vector<std::string> funForInitDeclareName;
std::vector<std::string> ignoreIncludeVector;
std::vector <std::string> functionNameVector;
ASTContext* globalContext;
CompilerInstance* compilerInstance;
std::vector<InitClassFun> initClassFunVector;
std::vector<HaveFunBodyHpp> haveFunBodyVector;
std::vector<std::string> allConstructionMethodVector;
AllHaveFunBodyHpp allHaveFunBodyHpp;
std::vector<AllFileContent> allFileContext;
/* 存储所有的文件信息 */
std::vector<std::string> fileContentVector;
std::string cPlusPlusPath;
static llvm::cl::OptionCategory MyToolCategory("global-detect options");
std::vector<Comment> CommentVector;
class CommentHandlerVisitor :public CommentHandler {
public:
	bool HandleComment(Preprocessor& PP, SourceRange Loc) override {
		SourceLocation Start = Loc.getBegin();
		SourceManager& SM = PP.getSourceManager();
		std::string C(SM.getCharacterData(Start),
			SM.getCharacterData(Loc.getEnd()));
		bool Invalid;
		unsigned CLine = SM.getSpellingLineNumber(Start, &Invalid);
		if (Invalid) {
			unsigned CCol = SM.getSpellingColumnNumber(Start, &Invalid);
			if (Invalid) {
				CommentVector.push_back(Comment(C, CLine, CCol));
			}
		}
		return false;
	}
};

CommentHandlerVisitor commentHandlerVisitor;
std::string GetComment(ASTContext* Context, const Decl* decl) {
	auto comment = Context->getCommentForDecl(decl, nullptr);
	std::string str = "";
	if (comment != nullptr) {
		for (auto commentItr = comment->child_begin(); commentItr != comment->child_end(); ++commentItr)
		{
			auto commentSection = *commentItr;

			if (commentSection->getCommentKind() != comments::BlockContentComment::ParagraphCommentKind)
			{
				continue;
			}

			for (auto textItr = commentSection->child_begin(); textItr != commentSection->child_end(); textItr++)
			{
				if (auto textComment = dyn_cast<comments::TextComment>(*textItr))
				{
					str += textComment->getText().ltrim();
					str += " ";
				}
			}
		}
	}
	return str;
}
bool isInIgnoreVector(std::string fileName) {
	std::vector<std::string>::iterator ret;
	ret = std::find(ignoreIncludeVector.begin(), ignoreIncludeVector.end(), fileName);
	if (ret == ignoreIncludeVector.end()) {
		return false;
	}
	else {
		return true;
	}
}
class Find_Includes : public PPCallbacks {
public:
	bool has_include;


	void InclusionDirective(SourceLocation hash_loc, const Token& include_token,
		StringRef file_name, bool is_angled,
		CharSourceRange filename_range, const FileEntry* file,
		StringRef search_path, StringRef relative_path,
		const clang::Module* imported,
		SrcMgr::CharacteristicKind FileType) {
		//auto& sourceManager = globalContext->getSourceManager();
		//std::string parserPath = sourceManager.getFilename(hash_loc).str();
		std::string fileName = relative_path;
		bool isParserFile = judgeIsParserFile(hash_loc, globalContext);
		if (isParserFile) {
			bool isInIgnore = isInIgnoreVector(fileName);
			if (!isInIgnore && !fileName.empty()) {
				fileContentVector.push_back("#include \"" + fileName + "\"");
			}
		}

		has_include = true;
	}
};
bool isParserd(SourceLocation sourceLoc,
	ASTContext* Context) {
	SourceManager& sourceManager = Context->getSourceManager();
	/*auto presumedLoc =sourceManager.getPresumedLoc(sourceLoc);
	if (!presumedLoc.isValid()) {
		return false;
	}*/
	int lineNum = sourceManager.getPresumedLoc(sourceLoc).getLine();
	std::vector<int>::iterator ret;
	ret = std::find(fileLocVector.begin(), fileLocVector.end(), lineNum);
	if (ret == fileLocVector.end()) {
		return false;
	}
	else {
		return true;
	}
}
bool judgeHasConstructionMethod(std::string className) {
	if (allConstructionMethodVector.size() > 0) {
		for (auto iter = allConstructionMethodVector.begin(); iter != allConstructionMethodVector.end(); iter++)
		{
			if (className + "_" + className == (*iter)) {
				return true;
			}
		}
	}
	return false;
}
std::string dealRecordStmt(std::string typeStr, bool isPointerType, std::string nameStr, std::string initValueStr, std::string dimStr) {
	bool isHasConstructionMethod = judgeHasConstructionMethod(typeStr);
	std::string paraName = "";
	if (isPointerType) {
		paraName = nameStr;
	}
	else {
		paraName = "&" + nameStr;
	}
	std::string initStructStr = "";
	if (isHasConstructionMethod) {
		initStructStr += typeStr + "_" + typeStr + "(" + paraName + ");";
	}
	else {
		initStructStr += typeStr + "_" + "init" + "(" + paraName + ");";
	}
	if (isPointerType) {
		typeStr += "*";
	}
	if (initValueStr == "") {

		return typeStr + " " + nameStr + dimStr + ";" + "\n\t" + initStructStr;
	}
	else {
		return typeStr + " " + nameStr + dimStr + "=" + initValueStr + ";" + "\n\t" + initStructStr;
	}
}
std::string dealVarDeclNode(VarDecl* var, ASTContext* Context) {
	std::string typeStr = var->getType().getLocalUnqualifiedType().getCanonicalType().getAsString();
	const Type* type = var->getType().getTypePtr();
	QualType nodeType = var->getType();
	/*std::string comment=GetComment(Context, var);
	const RawComment* rc = var->getASTContext().getRawCommentForDeclNoCache(var);*/
	if (type->getTypeClass() == 8) {
		const DecayedType* DT = type->getAs<DecayedType>();
		type = DT->getOriginalType().getTypePtr();
		nodeType = DT->getOriginalType();
	}
	std::string dimStr = "";
	typeStr = dealType(type, typeStr, nodeType, &dimStr);
	std::string initValueStr = "";
	const Expr* initExpr = var->getAnyInitializer();
	/* 处理全局变量的初值 */
	dealInitValue(initExpr, &initValueStr);

	std::string nameStr = var->getNameAsString();
	if (type->getTypeClass() == 36) {
		typeStr = type->getAsRecordDecl()->getNameAsString();
		return dealRecordStmt(typeStr, false, nameStr, initValueStr, dimStr);
	}
	else if (type->getTypeClass() == 34) {
		QualType pointType = type->getPointeeType();
		if (pointType.getTypePtr()->isStructureOrClassType()) {
			typeStr = pointType.getTypePtr()->getAsCXXRecordDecl()->getNameAsString();
			return dealRecordStmt(typeStr, true, nameStr, initValueStr, dimStr);
		}

	}

	/*if (auto *reclDecl=dyn_cast<CXXRecordDecl>(var->getType())) {
		typeStr = reclDecl->getNameAsString();
	}*/

	if (initValueStr == "") {
		return typeStr + " " + nameStr + dimStr + ";";
	}
	else {
		return typeStr + " " + nameStr + dimStr + "=" + initValueStr + ";";
	}

}
void addNodeInfo(SourceLocation sourceLoc,
	ASTContext* Context) {
	SourceManager& sourceManager = Context->getSourceManager();
	auto a = sourceManager.getPresumedLoc(sourceLoc);
	if (a.isValid()) {
		int lineNum = a.getLine();
		//std::string fileLoc = sourceManager.getFileLoc(sourceLoc).str();
		fileLocVector.push_back(lineNum);
	}

}
std::string dealDim(const Type* type, std::string dimStr) {

	/* 转换成数组类型 */
	const ArrayType* arrayType = type->castAsArrayTypeUnsafe();
	/* 处理ConstantArrayType类型，获取数组的维度 */
	if (arrayType->isConstantArrayType()) {
		int dim =
			dyn_cast<ConstantArrayType>(arrayType)->getSize().getLimitedValue();
		dimStr += std::to_string(dim);
		dimStr += "]";
	}
	/* 处理IncompleteArrayType类型 */
	else if (arrayType->isIncompleteArrayType()) {
		dimStr += "]";
	}

	/* 获取数组的类型 */
	const Type* childType = arrayType->getElementType().getTypePtr();

	/* 如果是ConstantArrayType或者IncompleteArrayType类型的数组 */
	if (childType->isConstantArrayType() || childType->isIncompleteArrayType()) {
		dimStr += "[";
		return dealDim(childType, dimStr);
	}
	else {
		return dimStr;
	}
}
QualType dealArrayType(const Type* type) {

	/* 转换成数组类型 */
	const ArrayType* arrayType = type->castAsArrayTypeUnsafe();
	/* 获取数组的维度 */

	/* 获取数组的类型 */
	const Type* childType = arrayType->getElementType()
		.getLocalUnqualifiedType()
		.getCanonicalType()
		.getTypePtr();
	/* 数组类型为ConstantArrayType()，如int[3] */
	/* 数组类型为IncompleteArrayType，如int[] */
	if (childType->isConstantArrayType() || childType->isIncompleteArrayType()) {
		return dealArrayType(childType);
	}
	else {
		QualType qualType = arrayType->getElementType()
			.getLocalUnqualifiedType()
			.getCanonicalType();
		return qualType;
	}
}
QualType dealPointerType(const Type* pointerType, std::string& pointer) {

	const Type* childType = pointerType->getPointeeType()
		.getLocalUnqualifiedType()
		.getCanonicalType()
		.getTypePtr();
	/* 如果子表达式仍为指针类型 */
	if (childType->isPointerType()) {
		pointer = pointer + "*";
		return dealPointerType(childType, pointer);
	}
	/* 如果不为指针类型 */
	else {
		QualType type = pointerType->getPointeeType();
		return type;
	}
}
std::string dealQualifiers(QualType nodeType,
	std::string typeStr) {
	std::string qualifier = nodeType.getQualifiers().getAsString();
	return qualifier;
}
std::string dealType(const Type* type, std::string typeStr, QualType nodeType, std::string* dimStr) {
	std::string qualifier = "";
	/* 说明有类型修饰符 */
	if (nodeType.hasQualifiers()) {
		qualifier = dealQualifiers(nodeType, typeStr);
	}
	/* 处理类型为ConstantArrayType的数组，如int[3] */
	/* 处理IncompleteArrayType类型的数组,如int[] */
	if (type->isConstantArrayType() || type->isIncompleteArrayType()) {
		/* 处理数组的类型 */
		QualType qualType = dealArrayType(type);
		typeStr = qualType.getAsString();
		/* 处理数组的维度 */
		*dimStr = "[";
		*dimStr = dealDim(type, *dimStr);
		type = qualType.getTypePtr();
		if (type->isPointerType()) {
			dealType(type, typeStr, nodeType, dimStr);
		}
		//typeStr = qualifier + " " + typeStr + " " + dimStr;
		typeStr = qualifier + " " + typeStr;
	}
	/* 如果是指针类型 */
	else if (type->isPointerType()) {
		/* 处理指针类型的* */
		std::string pointer = "*";
		/* 获取除*以外的类型 */
		QualType pointType = dealPointerType(type, pointer);
		typeStr = pointType.getAsString();
		/* 如果像包括const，volatile修饰符 */
		/*if (pointType.hasQualifiers()) {
			dealQualifiers(pointType, typeStr);
		}*/
		typeStr = pointType.getLocalUnqualifiedType().getCanonicalType().getAsString();
		/* 类型加指针 */
		std::string dimStr = pointer;
		typeStr = qualifier + " " + typeStr + pointer;
	}

	/* 如果不是数组 */
	else {
		if (qualifier != "") {
			typeStr = qualifier + " " + typeStr;
		}
	}
	auto idx = typeStr.find("struct");
	if (idx != string::npos) {
		typeStr.erase(idx, 6);
	}
	if (typeStr == "_Bool") {
		return "unsigned int";
	}
	return typeStr;
}
bool judgeConstStIsMethodStmt(const Stmt* st, ASTContext* astContext) {
	auto& parents = astContext->getParents(*st);
	if (parents.empty()) {
		return false;
	}
	/* 获取父节点 */
	const Stmt* currentSt = parents[0].get<Stmt>();
	if (!currentSt) {
		return false;
	}
	if (isa<CXXMemberCallExpr>(currentSt)) {
		return true;
	}
	else {
		return judgeConstStIsMethodStmt(currentSt, astContext);
	}
	return false;
}
bool judgeConstStIsCallExpr(const Stmt* st, ASTContext* astContext) {
	auto& parents = astContext->getParents(*st);
	if (parents.empty()) {
		return false;
	}
	/* 获取父节点 */
	const Stmt* currentSt = parents[0].get<Stmt>();
	if (!currentSt) {
		return false;
	}
	if (isa<CallExpr>(currentSt)) {
		return true;
	}
	else {
		return judgeConstStIsCallExpr(currentSt, astContext);
	}
	return false;
}
bool judgeConstStIsCallExpr(Stmt* st, ASTContext* astContext) {
	auto& parents = astContext->getParents(*st);
	if (parents.empty()) {
		return false;
	}
	/* 获取父节点 */
	const Stmt* currentSt = parents[0].get<Stmt>();
	if (!currentSt) {
		return false;
	}
	if (isa<CallExpr>(currentSt)) {
		return true;
	}
	else {
		return judgeConstStIsCallExpr(currentSt, astContext);
	}
	return false;
}
bool judgeConstStIsFunStmt(const Stmt* st, ASTContext* astContext) {
	auto& parents = astContext->getParents(*st);
	if (parents.empty()) {
		return false;
	}
	/* 获取父节点 */
	const Stmt* currentSt = parents[0].get<Stmt>();
	if (!currentSt) {
		return false;
	}
	if (isa<CompoundStmt>(currentSt)) {
		return true;
	}
	else {
		return judgeConstStIsFunStmt(currentSt, astContext);
	}
	return false;
}

bool judgeConstStIsFunDecl(const Decl* decl, ASTContext* astContext) {
	auto& parents = astContext->getParents(*decl);
	if (parents.empty()) {
		return false;
	}
	/* 获取父节点 */
	const Stmt* currentSt = parents[0].get<Stmt>();
	const Decl* currentDecl = parents[0].get<Decl>();
	if (!currentSt) {
		if (!currentDecl) {
			return false;
		}
		else {
			return judgeConstStIsFunDecl(currentDecl, astContext);
		}
	}
	if (isa<CompoundStmt>(currentSt)) {
		return true;
	}
	else {
		return judgeConstStIsFunStmt(currentSt, astContext);
	}
	return false;
}

bool judgeStIsFunStmt(Stmt* st, ASTContext* astContext) {
	/* 如果直接是函数或CompoundStmt节点，不用看parent */
	if (isa<CompoundStmt>(st)) {
		return true;
	}
	auto& parents = astContext->getParents(*st);
	if (parents.empty()) {
		return false;
	}
	/* 获取父节点 */
	const Stmt* currentSt = parents[0].get<Stmt>();
	const Decl* curentDecl = parents[0].get<Decl>();
	if (!currentSt) {
		if (!curentDecl) {
			return false;
		}
		else {
			return judgeConstStIsFunDecl(curentDecl, astContext);
		}
	}

	if (isa<CompoundStmt>(currentSt)) {
		return true;
	}
	else {
		return judgeConstStIsFunStmt(currentSt, astContext);
	}
	return false;
}

bool judgeIsHaveInitFun(CXXRecordDecl* recodeDecl) {
	std::string recodeName = recodeDecl->getNameAsString();
	RecordDecl::field_iterator jt;
	for (jt = recodeDecl->field_begin();
		jt != recodeDecl->field_end(); jt++) {
		auto a = jt->getType();
		auto c = a.getTypePtr();
	}
	//auto a=recodeDecl->begin();

	return true;
}
bool judgeIsClassFun(FunctionDecl* func) {
	auto declNode = func->getParent();
	if (dyn_cast<CXXRecordDecl>(declNode)) {
		return true;
	}
	else {
		return false;
	}
}


std::string getInitRecodeInVector(std::string funcName) {
	if (initClassFunVector.size() > 0) {
		for (auto iter = initClassFunVector.begin(); iter != initClassFunVector.end(); iter++)
		{
			if (funcName == (*iter).name) {
				std::string initStr = (*iter).initCompound;
				initClassFunVector.erase(iter);
				return initStr;
			}
		}
	}
	return "";
}

/* 处理结构体信息 */
void dealVarRecordDeclNode(CXXRecordDecl const* varRecordDeclNode, ASTContext* Context) {
	std::string recodeStr = "";
	std::string nameStr = "";
	std::string qualifier = "";
	if (nameStr.empty() && varRecordDeclNode->getTypedefNameForAnonDecl()) {
		/* 获取结构体的typedef名称 */
		nameStr = varRecordDeclNode->getTypedefNameForAnonDecl()->getNameAsString();
	}
	if (nameStr.empty()) {
		/* 直接获取结构体的名字 */
		nameStr = varRecordDeclNode->getNameAsString();
	}
	else {
		qualifier = "typedef ";
	}
	std::string structType = "";
	if (varRecordDeclNode->isUnion()) {
		structType = "union ";
	}
	else {
		structType = "struct ";
	}
	if (qualifier == "") {
		qualifier = "typedef ";
	}
	//if (varRecordDeclNode->isClass()) {
	recodeStr += qualifier + structType + "Class_" + nameStr + "\n" + "{\n\t";
	/*}
	else {
		recodeStr += qualifier + structType + nameStr + "\n" + "{\n\t";
	}*/

	/* 记录结构体的field数量 */
	RecordDecl::field_iterator jt;
	std::string initClassFunStr = "";
	if (varRecordDeclNode->field_begin() == varRecordDeclNode->field_end()) {
		recodeStr += "int _reserved_;\n\t";
	}
	for (jt = varRecordDeclNode->field_begin();
		jt != varRecordDeclNode->field_end(); jt++) {
		FieldDecl* field = *jt;
		addNodeInfo(field->getBeginLoc(), Context);
		std::string fieldNameStr = jt->getNameAsString();
		/* 获取全局变量的类型，为总类型 */
		std::string fieldTypeStr = jt->getType().getLocalUnqualifiedType().getCanonicalType().getAsString();
		/* 将全局变量的类型转成Type类型,便于后续判断是否为数组 */
		const Type* type = jt->getType().getTypePtr();
		QualType nodeType = jt->getType();
		std::string dimStr = "";
		fieldTypeStr = dealType(type, fieldTypeStr, nodeType, &dimStr);
		recodeStr += fieldTypeStr + " " + fieldNameStr + " " + dimStr + ";\n\t";
		std::string initValueStr = "";
		const Expr* initExpr = field->getInClassInitializer();;
		/* 处理全局变量的初值 */
		dealInitValue(initExpr, &initValueStr);
		if (!(initValueStr == "")) {
			initClassFunStr += "p" + nameStr + "->" + fieldNameStr + "=" + initValueStr + ";\n\t";
		}
	}
	if (!(initClassFunStr == "")) {
		InitClassFun initClassfun;
		initClassfun.name = nameStr;
		initClassfun.initCompound = initClassFunStr;
		initClassFunVector.push_back(initClassfun);
	}
	//if (varRecordDeclNode->isClass()) {
	recodeStr += "\r}" + nameStr + ";";
	//}
	//else {
	//	recodeStr += "\r};";
	//}
	fileContentVector.push_back(recodeStr);
	std::string initRecodeFunStr = getInitRecodeInVector(nameStr);
	if (!(initRecodeFunStr == "")) {
		std::string initFunStr = "";
		initFunStr += "void " + nameStr + "_init(" + nameStr + "* " + "p" + nameStr + ")\n{\t" + initRecodeFunStr + "\r}";
		fileContentVector.push_back(initFunStr);
	}
}


std::string getInitStrInVector(std::string funcName) {
	if (initClassFunVector.size() > 0) {
		for (auto iter = initClassFunVector.begin(); iter != initClassFunVector.end(); iter++)
		{
			if (funcName == (*iter).name + "_" + (*iter).name) {
				std::string initStr = (*iter).initCompound;
				initClassFunVector.erase(iter);
				return initStr;
			}
		}
	}
	return "";
}
std::string getNotReptatFunName(std::string funcName) {
	int count = 1;
	std::vector<std::string>::iterator ret;
	ret = std::find(functionNameVector.begin(), functionNameVector.end(), funcName);
	if (ret == functionNameVector.end()) {
		functionNameVector.push_back(funcName);
		return funcName;
	}
	else {
		return getNotReptatFunName(funcName.append(std::to_string(count++)));
	}
}
bool judgeIsParserFile(SourceLocation sourceLoc, ASTContext* Context) {
	SourceManager& sourceManager = Context->getSourceManager();
	std::string path = sourceManager.getFilename(sourceLoc).str().c_str();
	std::replace(path.begin(), path.end(), '\\', '/');
	std::replace(cPlusPlusPath.begin(), cPlusPlusPath.end(), '\\', '/');
	if (path == cPlusPlusPath) {
		return true;
	}
	else {
		return false;
	}
}
AllHaveFunBodyHpp getSingleHppFun() {
	if (hppFuns.size() > 0) {
		for (auto iter = hppFuns.begin(); iter != hppFuns.end(); iter++)
		{
			if ((strcmp(cPlusPlusPath.c_str(), (*iter).funPath.c_str()) == 0)) {
				return (*iter);
			}

		}
	}
	AllHaveFunBodyHpp funs;

	return funs;
}

class FindNamedClassVisitor
	: public RecursiveASTVisitor<FindNamedClassVisitor> {
public:
	explicit FindNamedClassVisitor(ASTContext* Context) : Context(Context) {}

private:
	ASTContext* astContext; // used for getting additional AST info

public:
	virtual bool VisitVarDecl(VarDecl* var) {
		bool isParserFile = judgeIsParserFile(var->getBeginLoc(), Context);
		if (isParserFile) {
			auto isInFunction = var->getParentFunctionOrMethod();
			if (!isInFunction) {
				if (!isParserd(var->getBeginLoc(), Context)) {
					addNodeInfo(var->getBeginLoc(), Context);
					fileContentVector.push_back(dealVarDeclNode(var, Context));
				}
			}
		}
		return true;
	}
	virtual bool VisitFunctionDecl(FunctionDecl* func) {
		funForInitDeclareName.clear();
		bool isParserFile = judgeIsParserFile(func->getLocation(), Context);
		if (isParserFile) {
			/*auto comm = Context->Comments;
			auto fid = Context->getSourceManager().getFileID(func->getLocation());
			auto comms = comm.getCommentsInFile(fid);
			&compilerInstance->getPreprocessor().HandleComment
			for (auto i : *comms) {
				llvm::outs() << i.second->getBriefText(*Context) << "\n";
			}*/

			/*	auto comment=Context->getCommentForDecl(func, nullptr);
				auto comment2= Context->getCommentForDecl(func, &compilerInstance->getPreprocessor());*/
			bool isClassFun = judgeIsClassFun(func);
			std::string funStr = "";
			std::string funcName = func->getNameInfo().getName().getAsString();
			std::string funcType = func->getType().getAsString();
			std::string returTypeStr = func->getReturnType().getAsString();
			if (returTypeStr == "_Bool") {
				returTypeStr = "unsigned int";
			}
			int paraNum = func->getNumParams();

			if (isClassFun) {
				CXXRecordDecl* cxxRecordDecl = dyn_cast<CXXRecordDecl>(func->getParent());
				auto className = cxxRecordDecl->getNameAsString();
				/* 构造析构函数的名称，判断是否是析构函数 */
				auto destructorName = "~" + className;
				if (destructorName == funcName) {
					funcName = className + "_destructor";
				}
				else {
					funcName = className + "_" + funcName;
				}
				funcName = getNotReptatFunName(funcName);
				funStr += returTypeStr + " " + funcName + "(";
				funStr += className + " *p" + className;
				if (paraNum != 0) {
					funStr += ",";
				}
			}
			else {
				funcName = getNotReptatFunName(funcName);
				funStr += returTypeStr + " " + funcName + "(";
			}
			if (paraNum != 0) {
				for (int i = 0; i < paraNum; i++) {
					std::string nameStr = func->getParamDecl(i)->getNameAsString();
					/* 获取函数参数的类型 */
					std::string typeStr = func->getParamDecl(i)->getType().getLocalUnqualifiedType().getCanonicalType().getAsString();
					/* 将函数参数的类型转换为Type,方便后续判断是否为数组类型 */
					const Type* type = func->getParamDecl(i)->getType().getTypePtr();
					// std::cout << type->getTypeClassName() << type->getTypeClass()<< "\n";
					/* 说明是Decayed类型,即本来是数组类型，被clang转换成指针类型 */
					QualType nodeType = func->getParamDecl(i)->getType();
					if (type->getTypeClass() == 8) {
						const DecayedType* DT = type->getAs<DecayedType>();
						type = DT->getOriginalType().getTypePtr();
						nodeType = DT->getOriginalType();
					}
					std::string dimStr = "";
					typeStr = dealType(type, typeStr, nodeType, &dimStr);
					auto idx = typeStr.find("struct");
					if (idx != string::npos) {
						typeStr.erase(idx, 6);
					}
					std::replace(typeStr.begin(), typeStr.end(), '&', '*');
					if (i == paraNum - 1) {
						funStr += typeStr + " " + nameStr + dimStr;
					}
					else {
						funStr += typeStr + " " + nameStr + dimStr + ",";
					}

				}
			}
			funStr += ")";
			/* 判断解析到的函数是否是类的构造函数且在Vector中 */
			std::string initClassFunStr = getInitStrInVector(funcName);
			if (!(initClassFunStr == "")) {
				allConstructionMethodVector.push_back(funcName);
				funStr += initClassFunStr;
			}
			/* 如果是类内的函数，则应该增加将类转换后的结构体 */
			Stmt* comStmtBody = func->getBody();
			if (comStmtBody) {
				std::string comStmtBodyStr = parserExpr(comStmtBody, Context);
				if (cPlusPlusPath.find(".hpp") != std::string::npos || cPlusPlusPath.find(".h") != std::string::npos) {
					//allHaveFunBodyHpp
					//allHaveFunBodyHpp = getSingleHppFun();
					//allHaveFunBodyHpp.funPath = strcpy(new char[cPlusPlusPath.length() + 1], cPlusPlusPath.data());
					HaveFunBodyHpp haveFunBodyHppStruct;
					haveFunBodyHppStruct.funDeclare = strcpy(new char[funStr.length() + 1], funStr.data());
					haveFunBodyHppStruct.funBody = strcpy(new char[comStmtBodyStr.length() + 1], comStmtBodyStr.data());
					allHaveFunBodyHpp.funs.push_back(haveFunBodyHppStruct);
					//haveFunBodyVector.push_back(haveFunBodyHppStruct);
					funStr += ";\n";
				}
				else {
					funStr += "\n{\n\t" + comStmtBodyStr + "\r}\n";
				}

			}
			else {
				if (cPlusPlusPath.find(".hpp") != std::string::npos || cPlusPlusPath.find(".h") != std::string::npos) {
					funStr += ";\n";
				}
				else {
					funStr += "\n{\n\t\r};";
				}

			}
			fileContentVector.push_back(funStr);
		}
		return true;
	}

	virtual bool VisitStmt(Stmt* st) {
		bool isParserFile = judgeIsParserFile(st->getBeginLoc(), Context);
		if (isParserFile && (!dyn_cast<IntegerLiteral>(st)) && (!dyn_cast<FloatingLiteral>(st))) {
			bool isFunctionStmt = judgeStIsFunStmt(st, Context);
			SourceManager& sourceManager = Context->getSourceManager();
			/* 如果不是函数中的stmt，在进行解析 */
			if (!isFunctionStmt) {
				if (!isParserd(st->getBeginLoc(), Context)) {
					std::string stmtStr = parserExpr(st, Context);
					fileContentVector.push_back(stmtStr);
				}
			}
		}
		return true;
	}

	virtual bool VisitCXXRecordDecl(CXXRecordDecl* recodeDecl) {
		bool isParserFile = judgeIsParserFile(recodeDecl->getBeginLoc(), Context);
		if (isParserFile) {
			//judgeIsHaveInitFun(recodeDecl);
			dealVarRecordDeclNode(recodeDecl, Context);
		}
		return true;
	}

private:
	ASTContext* Context;
};
/* 设置默认值 */
void getDefaultVaule(const ImplicitValueInitExpr* iV,
	std::string* defaultValueStr) {
	QualType ivType = iV->getType();
	/* double与float */
	if (ivType->isConstantArrayType() || ivType->isIncompleteArrayType()) {
		*defaultValueStr += "[" + std::to_string(0) + "]";
	}
	/* int */
	else if (ivType->isStructureType()) {
		*defaultValueStr += "{" + std::to_string(0) + "}";
	}
	else {
		*defaultValueStr += std::to_string(0);
	}
}
/* 处理全局变量的初值 */
void dealInitValue(const Expr* initExpr, std::string* initValueStr) {
	if (!(initExpr == NULL)) {
		/* double或float类型的初值 */
		if (isa<FloatingLiteral>(initExpr)) {
			const FloatingLiteral* fL = dyn_cast<FloatingLiteral>(initExpr);
			double initValue = fL->getValueAsApproximateDouble();
			/* 将float/double强转为String */
			*initValueStr += std::to_string(initValue);
		}
		/* int类型的value */
		if (isa<IntegerLiteral>(initExpr)) {
			const IntegerLiteral* iL = dyn_cast<IntegerLiteral>(initExpr);
			/* 将int强转为String */
			*initValueStr += std::to_string(iL->getValue().getSExtValue());
		}
		if (isa<ImplicitCastExpr>(initExpr)) {
			const ImplicitCastExpr* iCE = dyn_cast<ImplicitCastExpr>(initExpr);
			auto subExpr = iCE->getSubExpr();
			dealInitValue(subExpr, initValueStr);
		}
		/* 没有值的情况 */
		if (isa<ImplicitValueInitExpr>(initExpr)) {
			std::string defaultValueStr = "";
			const ImplicitValueInitExpr* iV =
				dyn_cast<ImplicitValueInitExpr>(initExpr);
			/* 将int强转为String */
			getDefaultVaule(iV, &defaultValueStr);
			*initValueStr += defaultValueStr;
		}
		/* 数组 */
		if (isa<InitListExpr>(initExpr)) {
			const InitListExpr* iLE = dyn_cast<InitListExpr>(initExpr);
			SourceLocation Loc = iLE->getSourceRange().getBegin();
			signed NumInits = iLE->getNumInits();
			// const Expr *temp = iLE->getInit(0);
			QualType initExprType = initExpr->getType();
			if (initExprType->isStructureType()) {
				*initValueStr += "\n{\n";
			}
			else {
				*initValueStr += "[";
			}

			for (int i = 0; i < NumInits; i++) {
				dealInitValue(iLE->getInit(i), initValueStr);
				if (initExprType->isStructureType() && (i != NumInits - 1)) {
					*initValueStr += ",\n";
				}
				else {
					*initValueStr += ",";
				}

			}
			int num = initValueStr->length();
			if (num > 0) {
				*initValueStr = initValueStr->substr(0, num - 1);
			}
			if (initExprType->isStructureType()) {
				*initValueStr += "\n}";
			}
			else {
				*initValueStr += "]";
			}
		}
	}
}
std::string  deleteColon(std::string exprStr) {
	if (exprStr.find(",") != std::string::npos) {
		return exprStr.substr(0, exprStr.length() - 1);
	}
	else {
		return exprStr;
	}
}
std::string  deleteComma(std::string exprStr) {
	if (exprStr.find(";") != std::string::npos) {
		return exprStr.substr(0, exprStr.length() - 1);
	}
	else {
		return exprStr;
	}
}
void dealSubIfStmt(Stmt* subOtherStmt, std::string& otherStmt, ASTContext* Context) {
	IfStmt* elseIfStmt = dyn_cast<IfStmt>(subOtherStmt);
	/* if语句的条件 */
	auto* elseIfCondExpr = elseIfStmt->getCond();
	std::string elseIfCondStr = parserExpr(elseIfCondExpr, Context);
	/* if语句体 */
	Stmt* elseIfComp = elseIfStmt->getThen();
	std::string rootIfCompStr = parserExpr(elseIfComp, Context);
	otherStmt += "\telse if(" + elseIfCondStr + "){\n\t" + rootIfCompStr + "}" + "\n";
	/* 获取其他的else-if/else语句 */
	Stmt* subElseIfStmt = elseIfStmt->getElse();
	if (subElseIfStmt) {
		if (IfStmt* subIfStmt = dyn_cast<IfStmt>(subElseIfStmt)) {
			dealSubIfStmt(subIfStmt, otherStmt, Context);
		}
		else if (CompoundStmt* elseStmt = dyn_cast<CompoundStmt>(subElseIfStmt)) {
			std::string elseCondStr = parserExpr(elseStmt, Context);
			otherStmt += "else\n{\n\t" + elseCondStr + "}" + "\n";
		}
	}
}
std::string deleteUnnecessarySign(std::string exprStr) {
	if (exprStr.length() >= 1) {
		if (exprStr.length() >= 2) {
			std::string suffix = exprStr.substr(exprStr.length() - 2, exprStr.length());
			if (suffix == "->") {
				exprStr = exprStr.substr(0, exprStr.length() - 2);
			}
		}
		std::string commaSuffix = exprStr.substr(exprStr.length() - 1, exprStr.length());
		if (commaSuffix == ".") {
			exprStr = exprStr.substr(0, exprStr.length() - 1);
		}
	}
	return exprStr;
}
bool judgeIsContainForDeclare(std::string nameStr) {
	std::vector<std::string>::iterator ret;
	ret = std::find(funForInitDeclareName.begin(), funForInitDeclareName.end(), nameStr);
	if (ret == funForInitDeclareName.end()) {
		funForInitDeclareName.push_back(nameStr);
		return false;
	}
	else {
		return true;
	}
}
std::string parserExpr(Stmt* stmt, ASTContext* Context) {
	addNodeInfo(stmt->getBeginLoc(), Context);
	switch (stmt->getStmtClass()) {
	case Expr::ImplicitCastExprClass:
	case Expr::CStyleCastExprClass:
	case Expr::CXXFunctionalCastExprClass:
	case Expr::CXXStaticCastExprClass:
	case Expr::CXXReinterpretCastExprClass:
	case Expr::CXXConstCastExprClass:
	{
		return parserExpr(cast<CastExpr>(stmt)->getSubExpr(), Context);
	}
	case Expr::CXXBoolLiteralExprClass: {
		CXXBoolLiteralExpr* cxxBoolLiteralExpr = dyn_cast<CXXBoolLiteralExpr>(stmt);
		bool boolExpr = cxxBoolLiteralExpr->getValue();
		if (boolExpr) {
			return "1";
		}
		else {
			return "0";
		}
	}
	case Expr::CXXDeleteExprClass: {
		CXXDeleteExpr* cxxDeleteExpr = dyn_cast<CXXDeleteExpr>(stmt);
		auto a = cxxDeleteExpr->getExprStmt();

		auto deleteArgument = cxxDeleteExpr->getArgument();
		auto deleteArgumentStr = parserExpr(deleteArgument, Context);
		return "/*delete [] " + deleteArgumentStr + ";*/\n\t";
	}
	case Expr::CXXOperatorCallExprClass: {
		CXXOperatorCallExpr* cxxOperatorCallExpr = dyn_cast<CXXOperatorCallExpr>(stmt);
		auto a = cxxOperatorCallExpr->getType();
		auto operateCallSubExprs = cxxOperatorCallExpr->getRawSubExprs();
		std::string operatorCallExprStr = "";
		std::string operatorCallExprStrArray[3];
		for (int i = 0; i < operateCallSubExprs.size(); i++) {
			operatorCallExprStr += parserExpr(operateCallSubExprs[i], Context);
			if (operateCallSubExprs.size() == 3) {

				operatorCallExprStrArray[i] = parserExpr(operateCallSubExprs[i], Context);
			}
		}
		if (operateCallSubExprs.size() == 3) {
			return operatorCallExprStrArray[1] + operatorCallExprStrArray[0] + operatorCallExprStrArray[2];
		}
		return operatorCallExprStr;
	}
	case Expr::CXXNullPtrLiteralExprClass: {
		return "0";
	}
	case Expr::ParenExprClass: {
		ParenExpr* parenExpr = dyn_cast<ParenExpr>(stmt);
		Expr* subParentExpr = parenExpr->getSubExpr();
		return "(" + parserExpr(subParentExpr, Context) + ")";
	}
	case Expr::SwitchStmtClass: {
		SwitchStmt* switchStmt = dyn_cast<SwitchStmt>(stmt);
		auto switchStmtCond = switchStmt->getCond();
		auto switchStmtBody = switchStmt->getBody();
		return "switch(" + parserExpr(switchStmtCond, Context) + ")\n\t{\n\t" + parserExpr(switchStmtBody, Context) + "\r}";
	}
	case Expr::CaseStmtClass: {
		CaseStmt* caseStmt = dyn_cast<CaseStmt>(stmt);
		return "case " + parserExpr(caseStmt->getLHS(), Context) + " :\n\t\t" + parserExpr(caseStmt->getSubStmt(), Context);
	}
	case Expr::BreakStmtClass: {
		return "\tbreak;\n";
	}
	case Expr::DefaultStmtClass: {
		DefaultStmt* defaultStmt = dyn_cast<DefaultStmt>(stmt);
		defaultStmt->getSubStmt();
		return "default :\n\t\t" + parserExpr(defaultStmt->getSubStmt(), Context) + "\n";
	}
	case Expr::ConstantExprClass: {
		ConstantExpr* constantExpr = dyn_cast<ConstantExpr>(stmt);
		return parserExpr(constantExpr->getSubExpr(), Context);
	}
	case Expr::BinaryOperatorClass: {
		BinaryOperator* binaryOperator = dyn_cast<BinaryOperator>(stmt);
		Expr* leftBinaryOperator = binaryOperator->getLHS();
		Expr* rightBinaryOperator = binaryOperator->getRHS();
		addNodeInfo(binaryOperator->getOperatorLoc(), Context);
		auto c = binaryOperator->getOpcode();
		switch (binaryOperator->getOpcode()) {
		case BO_Add: {
			return parserExpr(leftBinaryOperator, Context) + "+" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_Sub: {
			return parserExpr(leftBinaryOperator, Context) + "-" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_Mul: {
			return parserExpr(leftBinaryOperator, Context) + "*" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_Div: {
			return parserExpr(leftBinaryOperator, Context) + "/" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_LT: {
			return parserExpr(leftBinaryOperator, Context) + "<" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_GT: {
			return parserExpr(leftBinaryOperator, Context) + ">" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_LE: {
			return parserExpr(leftBinaryOperator, Context) + "<=" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_Rem: {
			return parserExpr(leftBinaryOperator, Context) + "%" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_GE: {
			return parserExpr(leftBinaryOperator, Context) + ">=" +
				parserExpr(rightBinaryOperator, Context);
		}
		case BO_Assign: {
			std::string leftExprStr = parserExpr(leftBinaryOperator, Context);
			std::string rightExprStr = parserExpr(rightBinaryOperator, Context);
			leftExprStr = deleteUnnecessarySign(leftExprStr);
			rightExprStr = deleteUnnecessarySign(rightExprStr);
			return leftExprStr + "=" + rightExprStr;
		}
		case BO_Shl: {
			return parserExpr(leftBinaryOperator, Context) + "<<" +
				parserExpr(rightBinaryOperator, Context);
		}

				   case BO_EQ
				   : {
					   return parserExpr(leftBinaryOperator, Context) + "==" +
						   parserExpr(rightBinaryOperator, Context);
				   }
				   case BO_LAnd: {
					   return parserExpr(leftBinaryOperator, Context) + "&&" +
						   parserExpr(rightBinaryOperator, Context);
				   }
				   case BO_LOr: {
					   return parserExpr(leftBinaryOperator, Context) + "||" +
						   parserExpr(rightBinaryOperator, Context);
				   }
				   case BO_NE: {
					   return parserExpr(leftBinaryOperator, Context) + "!=" +
						   parserExpr(rightBinaryOperator, Context);
				   }
				   case BO_AddAssign: {
					   return parserExpr(leftBinaryOperator, Context) + "+=" +
						   parserExpr(rightBinaryOperator, Context);
				   }
				   default: {
					   return "BinaryOperatorClass";
					   break;
				   }

		}
	}
	case Expr::ArraySubscriptExprClass: {
		ArraySubscriptExpr* arraySubscriptExpr = dyn_cast<ArraySubscriptExpr>(stmt);
		Expr* arrayBase = arraySubscriptExpr->getBase();
		std::string arrayName = parserExpr(arrayBase, Context);
		Expr* subExpr = arraySubscriptExpr->getRHS();
		auto dimStr = parserExpr(arraySubscriptExpr->getRHS(), Context);
		return arrayName + "[" + dimStr + "]";
	}
	case Expr::MemberExprClass: {
		MemberExpr* memberExpr = dyn_cast<MemberExpr>(stmt);
		std::string className = "";
		auto cxxThisExprNode = memberExpr->getBase();
		className = parserExpr(cxxThisExprNode, Context);
		auto fildName = memberExpr->getMemberDecl()->getDeclName().getAsString();
		auto stmtClassType = cxxThisExprNode->getStmtClass();
		if (stmtClassType == Expr::CXXThisExprClass) {
			bool isCallExpr = judgeConstStIsCallExpr(stmt, Context);
			return  className + "->" + fildName;
		}
		auto type = cxxThisExprNode->getType();
		auto classType = type->getTypeClass();
		if (type->getTypeClass() == 34) {
			QualType pointType = type->getPointeeType();
			if (pointType.getTypePtr()->isStructureOrClassType()) {
				//return pointType.getTypePtr()->getAsCXXRecordDecl()->getNameAsString() + "->";
				return  className + "->" + fildName;
			}

		}
		else if (type->getTypeClass() == 36) {
			std::string startStr = className.substr(0, 1);
			if (startStr == "*") {
				return  className.substr(1, className.length()) + "->" + fildName;
			}
			else {
				return  className + "." + fildName;
			}

		}
		/*else if (className.find(">") != std::string::npos) {

		}*/
		else {
			return  className + fildName;
		}
	}
	case Expr::CXXThisExprClass: {
		CXXThisExpr* cxxThisExpr = dyn_cast<CXXThisExpr>(stmt);
		auto cxxThisExprType = cxxThisExpr->getType().getTypePtr();
		auto className = cxxThisExpr->getType().getBaseTypeIdentifier()->getName();
		std::string classNameStr = className;
		auto a = cxxThisExprType->getTypeClass();
		return "p" + classNameStr;
	}
	case Expr::IntegerLiteralClass: {
		IntegerLiteral* integerLiteral = dyn_cast<IntegerLiteral>(stmt);
		int intValue = integerLiteral->getValue().getZExtValue();
		return to_string(intValue);


	}case Expr::FloatingLiteralClass: {
		FloatingLiteral* floatingLiteral = dyn_cast<FloatingLiteral>(stmt);
		auto doubleValue = floatingLiteral->getValue().convertToDouble();
		/*llvm::SmallVector<char, 32> floatValue;
		floatingLiteral->getValue().toString(floatValue, 32, 0);*/
		return to_string(doubleValue);
	}
	case Expr::IfStmtClass: {
		IfStmt* rootIfStmt = dyn_cast<IfStmt>(stmt);
		/* if语句的条件 */
		auto* rootIfCondExpr = rootIfStmt->getCond();
		std::string rootIfCondStr = parserExpr(rootIfCondExpr, Context);
		/* if语句体 */
		Stmt* rootIfComp = rootIfStmt->getThen();
		std::string rootIfCompStr = parserExpr(rootIfComp, Context);
		/* 获取其他的else-if/else语句 */
		Stmt* subOtherStmt = rootIfStmt->getElse();
		/* 说明至少含有一个else语句 */
		if (subOtherStmt) {
			std::string otherStmt;
			/* 说明是if-elseif-else语句*/
			if (IfStmt* subIfStmt = dyn_cast<IfStmt>(subOtherStmt)) {
				dealSubIfStmt(subOtherStmt, otherStmt, Context);
				auto a = "if(" + rootIfCondStr + "){" + "\n\t" + rootIfCompStr + "}" + "\n" + otherStmt;
				return a;
			}
			/* 说明是else语句 */
			else if (CompoundStmt* elseStmt = dyn_cast<CompoundStmt>(subOtherStmt)) {
				std::string elseCondStr = parserExpr(elseStmt, Context);
				return "if(" + rootIfCondStr + "){" + "\n\t" + rootIfCompStr + "}" + "\n\t" + "else\n\t{\t" + elseCondStr + "}" + "\n";
			}
		}
		else {
			return "if(" + rootIfCondStr + "){" + "\n\t" + rootIfCompStr + "}" + "\n";
		}

	}
	case Expr::CompoundAssignOperatorClass: {
		CompoundAssignOperator* compoundAssignOperator = dyn_cast<CompoundAssignOperator>(stmt);
		Expr* leftCompondAssign = compoundAssignOperator->getLHS();
		Expr* rightCompondAssign = compoundAssignOperator->getRHS();
		return parserExpr(leftCompondAssign, Context) + "+=" + parserExpr(rightCompondAssign, Context);
	}
	case Expr::CompoundStmtClass: {
		std::string compStmtBodyStr = "";
		CompoundStmt* compStmt = dyn_cast<CompoundStmt>(stmt);
		if (compStmt) {
			for (auto compStmtChild = compStmt->child_begin();
				compStmtChild != compStmt->child_end(); compStmtChild++) {
				Stmt* exprStmt = *compStmtChild;

				std::string singleCompStmtBodyStr = parserExpr(exprStmt, Context);
				if (!singleCompStmtBodyStr.empty()) {
					if (singleCompStmtBodyStr.find(";") != std::string::npos) {
						compStmtBodyStr += singleCompStmtBodyStr + "\n\t";
					}
					else if (singleCompStmtBodyStr.find("}") != std::string::npos) {
						compStmtBodyStr += singleCompStmtBodyStr + "\n\t";
					}
					else {
						compStmtBodyStr += singleCompStmtBodyStr + ";\n\t";
					}

				}
			}
		}
		return compStmtBodyStr;

	}
	case Expr::ForStmtClass: {
		ForStmt* forStmt = dyn_cast<ForStmt>(stmt);
		Stmt* forInit = forStmt->getInit();
		std::string forDeclStr = "";
		Expr* forCond = forStmt->getCond();
		Expr* forInc = forStmt->getInc();
		std::string forInitStr = parserExpr(forInit, Context);
		forInitStr = deleteComma(forInitStr);
		if (dyn_cast<DeclStmt>(forInit)) {
			DeclStmt* forInitStmt = dyn_cast<DeclStmt>(forInit);
			if (forInitStmt->isSingleDecl()) {
				auto declExprNode = forInitStmt->getSingleDecl();
				if (VarDecl* varDeclNode = dyn_cast<VarDecl>(declExprNode)) {
					std::string typeStr = varDeclNode->getType().getAsString();
					std::string nameStr = varDeclNode->getNameAsString();
					bool isContainForDeclare = judgeIsContainForDeclare(nameStr);
					if (!isContainForDeclare) {
						forDeclStr = typeStr + " " + nameStr + ";\n\t";
					}
					auto idx = forInitStr.find(typeStr);
					if (idx != string::npos) {
						forInitStr = forInitStr.substr(idx + typeStr.length() + 1, forInitStr.length());

					}
				}

			}
		}
		std::string forCondStr = parserExpr(forCond, Context);
		forCondStr = deleteComma(forCondStr);
		std::string forIncStr = parserExpr(forInc, Context);
		forIncStr = deleteComma(forIncStr);
		Stmt* forBody = forStmt->getBody();
		std::string forBodyStr = parserExpr(forBody, Context);
		return forDeclStr + "for(" + forInitStr + ";" + forCondStr + ";" + forIncStr + "){\n\t" + forBodyStr + "}";
	}
	case Expr::UnaryOperatorClass: {
		UnaryOperator* unaryOperatorExpr = dyn_cast<UnaryOperator>(stmt);
		addNodeInfo(unaryOperatorExpr->getBeginLoc(), Context);
		switch (unaryOperatorExpr->getOpcode()) {
		case UO_PostInc: {
			return parserExpr(unaryOperatorExpr->getSubExpr(), Context) + "++";
		}
		case UO_PostDec: {
			return parserExpr(unaryOperatorExpr->getSubExpr(), Context) + "--";
		}
		case UO_Minus: {
			return "-" + parserExpr(unaryOperatorExpr->getSubExpr(), Context);
		}
		case UO_Deref: {
			return "*" + parserExpr(unaryOperatorExpr->getSubExpr(), Context);
		}
		case UO_LNot: {
			auto a = "!" + parserExpr(unaryOperatorExpr->getSubExpr(), Context);
			return "!" + parserExpr(unaryOperatorExpr->getSubExpr(), Context);
		}
		default: {
			return "UnaryOperatorClass";
		}
		}
		break;
	}
	case Expr::DeclRefExprClass: {

		DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(stmt);
		ValueDecl* valueDecl = declRefExpr->getDecl();
		addNodeInfo(valueDecl->getBeginLoc(), Context);
		auto type = valueDecl->getType();
		if (type->getTypeClass() == 36) {
			bool isMemberCallExpr = judgeConstStIsMethodStmt(declRefExpr, Context);
			if (isMemberCallExpr) {
				return type->getAsRecordDecl()->getNameAsString() + "_";
			}
			//return type->getAsRecordDecl()->getNameAsString() + ".";
			return valueDecl->getNameAsString() + ".";
		}
		else if (type->getTypeClass() == 34) {
			QualType pointType = type->getPointeeType();
			if (pointType.getTypePtr()->isStructureOrClassType()) {
				//return pointType.getTypePtr()->getAsCXXRecordDecl()->getNameAsString() + "->";
				return valueDecl->getNameAsString() + "->";
			}

		}
		auto a = type->getTypeClass();
		auto d = declRefExpr->isLValue();
		/*else if (type->getTypeClass() == 21) {
			bool isConstStIsCallExpr = judgeConstStIsCallExpr(stmt, Context);
			if (isConstStIsCallExpr) {
				return valueDecl->getNameAsString();
			}
			else {
				return "";
			}
		}*/
		auto valueDeclStr = valueDecl->getNameAsString();
		if (valueDeclStr == "operator=") {
			return "=";
		}
		else if (type->getTypeClass() == 24) {
			return "*" + valueDecl->getNameAsString();
		}
		else {
			return valueDecl->getNameAsString();
		}

	}
	case Expr::WhileStmtClass: {
		WhileStmt* whileStmt = dyn_cast<WhileStmt>(stmt);
		Expr* whileCond = whileStmt->getCond();
		std::string whileCondStr = parserExpr(whileCond, Context);
		Stmt* whileBody = whileStmt->getBody();
		std::string whileBodyStr = parserExpr(whileBody, Context);
		return "while(" + whileCondStr + "){\n" + whileBodyStr + "}";
	}
	case Expr::ReturnStmtClass: {
		ReturnStmt* returnStmt = dyn_cast<ReturnStmt>(stmt);
		Expr* expr = returnStmt->getRetValue();
		if (expr) {
			return "return " + parserExpr(expr, Context) + ";";
		}
		else {
			return "return;\n";
		}

	}
							  /*case Expr::CXXMemberCallExprClass: {
								  CXXMemberCallExpr* cXXMemberCallExpr = dyn_cast<CXXMemberCallExpr>(stmt);
								  FunctionDecl* cXX_func_decl =cXXMemberCallExpr->getDirectCallee();
							  }*/
	case Expr::CallExprClass:
	case Expr::CXXMemberCallExprClass:
	{
		CallExpr* call = dyn_cast<CallExpr>(stmt);
		auto func_decl = call->getCallee();
		auto funcCall = parserExpr(func_decl, Context);
		std::string callArgsStr = "";
		if (dyn_cast<MemberExpr>(func_decl)) {
			auto memberExpr = dyn_cast<MemberExpr>(func_decl);
			auto memberBase = memberExpr->getBase();
			if (dyn_cast<CXXThisExpr>(memberBase)) {

				auto idx = funcCall.find(">");
				if (idx != string::npos) {
					auto cxxThisExpr = dyn_cast<CXXThisExpr>(memberBase);
					auto className = cxxThisExpr->getType().getBaseTypeIdentifier()->getName();
					std::string classNameStr = className;
					std::string filedName = funcCall.substr(idx + 1, funcCall.length());
					funcCall = classNameStr + "_" + filedName;
					callArgsStr += "p" + classNameStr;
				}

			}
			if (dyn_cast<DeclRefExpr>(memberBase)) {
				auto declRefExpr = dyn_cast<DeclRefExpr>(memberBase);

				ValueDecl* valueDecl = declRefExpr->getDecl();
				auto type = valueDecl->getType();
				if (type->getTypeClass() == 36) {
					callArgsStr += "&" + valueDecl->getNameAsString();
				}
				else if (type->getTypeClass() == 34) {
					QualType pointType = type->getPointeeType();
					if (pointType.getTypePtr()->isStructureOrClassType()) {

						callArgsStr += valueDecl->getNameAsString();
					}

				}
			}
		}
		callArgsStr = deleteColon(callArgsStr);
		if (call->getNumArgs() > 0 && (!(callArgsStr == ""))) {
			callArgsStr += ",";
		}
		for (int i = 0, j = call->getNumArgs(); i < j; i++) {
			Expr* callArg = call->getArg(i);
			bool isLValue = callArg->isLValue();
			auto singleCallArg = parserExpr(callArg, Context);
			singleCallArg = deleteColon(singleCallArg);
			if (isLValue) {
				singleCallArg = "&(" + singleCallArg + ")";
			}
			callArgsStr += singleCallArg;
			if (i != call->getNumArgs() - 1) {
				callArgsStr += ",";
			}
			/*else {
				callArgsStr += parserExpr(callArg, Context) + ",";
			}*/

		}
		return funcCall + "(" + callArgsStr + ")";
	}
	case Expr::DeclStmtClass: {
		DeclStmt* declStmt = dyn_cast<DeclStmt>(stmt);
		if (declStmt->isSingleDecl()) {
			auto declExprNode = declStmt->getSingleDecl();
			addNodeInfo(declExprNode->getBeginLoc(), Context);
			if (VarDecl* varDeclNode = dyn_cast<VarDecl>(declExprNode)) {
				return dealVarDeclNode(varDeclNode, Context);
			}

		}
		else {
			std::string compDecl = "";
			DeclGroupRef declGroupRef = declStmt->getDeclGroup();
			for (auto declChild = declGroupRef.begin(); declChild != declGroupRef.end(); declChild++) {
				Decl* singleDeclStmt = *declChild;
				if (VarDecl* varDeclNode = dyn_cast<VarDecl>(singleDeclStmt)) {
					compDecl += dealVarDeclNode(varDeclNode, Context) + "\n\t";
				}
			}
			return compDecl;
		}
	}
	case Expr::CXXConstructExprClass: {
		CXXConstructExpr* cxxConstructExpr = dyn_cast<CXXConstructExpr>(stmt);
		auto a = cxxConstructExpr->getArgs();
		std::string constructExprStr = "";
		for (auto subConstructExpr = cxxConstructExpr->child_begin(); subConstructExpr != cxxConstructExpr->child_end(); subConstructExpr++) {
			Stmt* subConStmt = *subConstructExpr;
			constructExprStr += parserExpr(subConStmt, Context);
		}
		return constructExprStr;
	}
	case Expr::UnaryExprOrTypeTraitExprClass: {
		UnaryExprOrTypeTraitExpr* unaryExprOrTypeTraitExpr = dyn_cast<UnaryExprOrTypeTraitExpr>(stmt);
		auto sizeType = unaryExprOrTypeTraitExpr->getArgumentType();
		auto sizeNameStr = sizeType.getTypePtr()->getAsCXXRecordDecl()->getNameAsString();
		//Expr* unaryExprOrTypeTraitExprArgu=unaryExprOrTypeTraitExpr->getArgumentExpr();
		//clang::Expr::EvalResult integer;
		//unaryExprOrTypeTraitExpr.
		//unaryExprOrTypeTraitExpr->EvaluateAsInt(integer,*Context,);
		return "sizeof(" + sizeNameStr + ")";
	}

	default: {
		return "";

	}

	}
}
class FindNamedClassConsumer : public clang::ASTConsumer {
public:
	explicit FindNamedClassConsumer(ASTContext* Context) : Visitor(Context) {}

	virtual void HandleTranslationUnit(clang::ASTContext& Context) {
		Visitor.TraverseDecl(Context.getTranslationUnitDecl());
	}

private:
	FindNamedClassVisitor Visitor;
};

class FindNamedClassAction : public clang::ASTFrontendAction {
public:
	virtual std::unique_ptr<clang::ASTConsumer>
		CreateASTConsumer(clang::CompilerInstance& Compiler, llvm::StringRef InFile) {
		globalContext = &Compiler.getASTContext();
		compilerInstance = &Compiler;
		Preprocessor& pp = Compiler.getPreprocessor();
		Find_Includes* find_includes_callback =
			static_cast<Find_Includes*>(pp.getPPCallbacks());
		return std::unique_ptr<clang::ASTConsumer>(
			new FindNamedClassConsumer(&Compiler.getASTContext()));
	}
	bool BeginSourceFileAction(CompilerInstance& ci) {
		std::unique_ptr<Find_Includes> find_includes_callback(new Find_Includes());
		

		Preprocessor& pp = ci.getPreprocessor();
		
	/*	CommentHandlerVisitor* V =
			static_cast<CommentHandlerVisitor*>(find_includes_callback);*/
		//pp.addCommentHandler(V);
		pp.addCommentHandler(&commentHandlerVisitor);
		pp.addPPCallbacks(std::move(find_includes_callback));
		return true;
	}
	void EndSourceFileAction() {
		CompilerInstance& ci = getCompilerInstance();
		Preprocessor& pp = ci.getPreprocessor();
		Find_Includes* find_includes_callback =
			static_cast<Find_Includes*>(pp.getPPCallbacks());
		// do whatever you want with the callback now
		if (find_includes_callback->has_include) {
			std::cout << "Found at least one include" << std::endl;
		}
	}
};
void initIgnoreVecotr() {
	ignoreIncludeVector.push_back("iostream");
	ignoreIncludeVector.push_back("string");
	ignoreIncludeVector.push_back("fstream");
	ignoreIncludeVector.push_back("tchar.h");
	ignoreIncludeVector.push_back("windows.h");
}
void convertCPlusPlus2C(std::string sourcePath, std::string writePath) {
	functionNameVector.clear();
	fileContentVector.clear();
	cPlusPlusPath = sourcePath;
	allHaveFunBodyHpp.funPath = strcpy(new char[cPlusPlusPath.length() + 1], cPlusPlusPath.data());
	int a = 4;
	std::replace(sourcePath.begin(), sourcePath.end(), '\\', '/');
	string::size_type iPos = sourcePath.find_last_of('/') + 1;
	string fileNameWithSuffixStr = sourcePath.substr(iPos, sourcePath.length() - iPos);
	string suffixStr = fileNameWithSuffixStr.substr(fileNameWithSuffixStr.find_last_of('.') + 1);
	string fileNameStr = fileNameWithSuffixStr.substr(0, fileNameWithSuffixStr.rfind("."));
	if (suffixStr == "h" || suffixStr == "hpp") {
		fileContentVector.push_back("#ifndef _" + fileNameStr + "_H_");
		fileContentVector.push_back("#define _" + fileNameStr + "_H_");
	}
	const char* argvs1212[4] = { "", "", "--","-DNULL=nullptr" };
	CommonOptionsParser OptionsParser(a, argvs1212, MyToolCategory);
	// run the Clang Tool, creating a new FrontendAction (explained below)
	std::vector<std::string> fileList;
	fileList.push_back(sourcePath);
	ClangTool Tool(OptionsParser.getCompilations(), fileList);

	Tool.run(newFrontendActionFactory<FindNamedClassAction>().get());
	if (suffixStr == "h" || suffixStr == "hpp") {
		fileContentVector.push_back("#endif");
	}
	hppFuns.push_back(allHaveFunBodyHpp);
	AllFileContent singleFileContext;
	singleFileContext.writePath = strcpy(new char[writePath.length() + 1], writePath.data());
	singleFileContext.fileContent = fileContentVector;
	allFileContext.push_back(singleFileContext);
}
void addHppFunBody() {
	if (hppFuns.size() > 0) {
		for (auto iter = hppFuns.begin(); iter != hppFuns.end(); iter++)
		{
			if ((*iter).funs.size() > 0) {
				std::string funPathStr = (*iter).funPath;
				std::vector<HaveFunBodyHpp> funsVector = (*iter).funs;
				std::replace(funPathStr.begin(), funPathStr.end(), '\\', '/');
				string::size_type iPos = funPathStr.find_last_of('/') + 1;
				string fileNameWithSuffixStr = funPathStr.substr(iPos, funPathStr.length() - iPos);
				string suffixStr = fileNameWithSuffixStr.substr(fileNameWithSuffixStr.find_last_of('.') + 1);
				string fileNameStr = fileNameWithSuffixStr.substr(0, fileNameWithSuffixStr.rfind("."));
				if (suffixStr == "h" || suffixStr == "hpp") {
					/* 判断对应的c文件是否存在 */
					std::string nameWithoutSuffix = funPathStr.substr(0, funPathStr.rfind("."));
					ifstream fin(nameWithoutSuffix + ".c");
					ofstream write;
					/* 文件不存在 */
					if (!fin) {
						write.open(nameWithoutSuffix + ".c");
						write << "#include \"" + nameWithoutSuffix + ".h\"\n" << endl;
					}
					else {
						write.open(nameWithoutSuffix + ".c", ios::app);
					}
					for (auto funIter = funsVector.begin(); funIter != funsVector.end(); funIter++) {
						write << (*funIter).funDeclare << endl;
						write << "\n{\n\t" << endl;
						write << (*funIter).funBody << endl;
						write << "\n}\n" << endl;
					}
				}
			}
		}
	}
}
void convertFiles(const char** sourcePath, const char** targetPath, int fileCount) {
	hppFuns.clear();
	initIgnoreVecotr();
	for (int i = 0; i < fileCount; i++) {
		memset(&allHaveFunBodyHpp, 0, sizeof(allHaveFunBodyHpp));
		convertCPlusPlus2C(sourcePath[i], targetPath[i]);
	}
	if (allFileContext.size() > 0) {
		std::ofstream fout;
		for (auto iter = allFileContext.begin(); iter != allFileContext.end(); iter++)
		{
			std::ofstream fout;
			fout.open(iter->writePath);
			for (auto subIter = iter->fileContent.begin(); subIter != iter->fileContent.end(); subIter++)
			{
				fout << *subIter << endl;
			}
			fout.close();

		}
	}
	/*std::ofstream fout;
	fout.open(writePath);
	if (fileContentVector.size() > 0) {
		for (auto iter = fileContentVector.begin(); iter != fileContentVector.end(); iter++)
		{
			fout << *iter << endl;
		}
	}
	fout.close();*/
	addHppFunBody();
}
int main(int argc, const char** argv) {
	const char* s[1] = {
				"C:\\Users\\bondc\\Desktop\\a.hpp",
	};

	const char* t[1] = {
				"C:\\Users\\bondc\\Desktop\\a.c"

	};
	convertFiles(s, t, 1);
	CommentVector.clear();
}
//int main(int argc, const char** argv) {
//
//	/*if (initClassFunVector.size() > 0) {
//		for (auto iter = initClassFunVector.begin(); iter != initClassFunVector.end(); iter++)
//		{
//			std::string initFunStr = "";
//			initFunStr += "void " + (*iter).name + "_init(" + (*iter).name + "* " + "p" + (*iter).name + ")\n{\t" + (*iter).initCompound + "\r}";
//			fileContentVector.push_back(initFunStr);
//		}
//	}*/
//	hppFuns.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\data.c", "C:\\Users\\bondc\\Desktop\\a.c");
//	fileContentVector.clear();
//	/*convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\common_function.cpp", "C:\\Users\\bondc\\Desktop\\test\\common_function.c");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\CtrlEx_inner_algo.cpp", "C:\\Users\\bondc\\Desktop\\test\\CtrlEx_inner_algo.c");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\CtrlEx_solve.cpp", "C:\\Users\\bondc\\Desktop\\test\\CtrlEx_solve.c");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\CtrlEx_std_algo.cpp", "C:\\Users\\bondc\\Desktop\\test\\CtrlEx_std_algo.c");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\common_function.hpp", "C:\\Users\\bondc\\Desktop\\test\\common_function.h");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\CtrlEx.hpp", "C:\\Users\\bondc\\Desktop\\test\\CtrlEx.h");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\CtrlEx_type.hpp", "C:\\Users\\bondc\\Desktop\\test\\CtrlEx_type.h");
//	fileContentVector.clear();
//	convertCPlusPlus2C ("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\include\\JETINPUT.hpp", "C:\\Users\\bondc\\Desktop\\test\\JETINPUT.h");
//	fileContentVector.clear();
//	convertCPlusPlus2C("C:\\Users\\bondc\\Desktop\\CtrlEx\\CtrlEx\\include\\GYROOUTPUT.hpp", "C:\\Users\\bondc\\Desktop\\test\\GYROOUTPUT.h");
//	fileContentVector.clear();*/
//	addHppFunBody();
//	return 0;
//}