#include <rose.h>
#include <string>
#include <boost/foreach.hpp>
#include "RtedSymbols.h"
#include "DataStructures.h"
#include "RtedTransformation.h"
//#include "RuntimeSystem.h"

using namespace std;
using namespace SageInterface;
using namespace SageBuilder;


/* -----------------------------------------------------------
 * Run frontend and return project
 * -----------------------------------------------------------*/
SgProject*
RtedTransformation::parse(int argc, char** argv) {
  SgProject* project = frontend(argc, argv);
  ROSE_ASSERT(project);
  return project;
}



/* -----------------------------------------------------------
 * Perform all transformations needed (Step 2)
 * -----------------------------------------------------------*/
void RtedTransformation::transform(SgProject* project) {
  cout << "Running Transformation..." << endl;
  globalScope = getFirstGlobalScope(isSgProject(project));

  ROSE_ASSERT( project);

  // traverse the AST and find locations that need to be transformed
  symbols->traverse(project, preorder);
  roseCreateArray = symbols->roseCreateArray;
  roseArrayAccess = symbols->roseArrayAccess;
  roseFunctionCall = symbols->roseFunctionCall;
  roseRtedClose = symbols->roseRtedClose;
  roseConvertIntToString=symbols->roseConvertIntToString;
  roseCallStack = symbols->roseCallStack;
  roseCreateVariable = symbols->roseCreateVariable;
  roseInitVariable = symbols->roseInitVariable;
  roseAccessVariable = symbols->roseAccessVariable;
  roseEnterScope = symbols->roseEnterScope;
  roseExitScope = symbols->roseExitScope;


  ROSE_ASSERT(roseCreateArray);
  ROSE_ASSERT(roseArrayAccess);
  ROSE_ASSERT(roseConvertIntToString);
  ROSE_ASSERT(roseRtedClose);
  ROSE_ASSERT(roseCallStack);
  ROSE_ASSERT(roseCreateVariable);
  ROSE_ASSERT(roseInitVariable);
  ROSE_ASSERT(roseAccessVariable);
  ROSE_ASSERT(roseEnterScope);
  ROSE_ASSERT(roseExitScope);


  traverseInputFiles(project,preorder);


  // ---------------------------------------
  // Perform all transformations...
  //
  // Do insertions LIFO, so, e.g. if we want to add stmt1; stmt2; after stmt0
  // add stmt2 first, then add stmt1
  // ---------------------------------------


  // bracket function calls and scope statements with calls to enterScope and
  // exitScope.  
  //
  // Note: For function calls, this must occur before variable
  // initialization, so that assignments of function return values happen before
  // exitScope is called.
  cerr << "\n Number of Elements in scopes  : "
       << scopes.size() << endl;
  std::vector<SgStatement*>::const_iterator stmtIt =
    scopes.begin();
  for (; stmtIt != scopes.end(); stmtIt++) {
	  bracketWithScopeEnterExit( *stmtIt);
  }

  // before we insert the intitialized variables,
  // we need to insert the temporary statements that
  // we found during our traversal
  cerr
    << "\n Number of Elements in variableIsInitialized  : "
    << variableIsInitialized.size() << endl;
  std::map<SgStatement*,SgStatement*>::const_iterator itStmt =
    insertThisStatementLater.begin();
  for (; itStmt != insertThisStatementLater.end(); itStmt++) {
    SgStatement* newS = itStmt->first;
    SgStatement* old = itStmt->second;
    insertStatementAfter(old,newS);
  }
  std::map<SgVarRefExp*,std::pair<SgInitializedName*,bool> >::const_iterator it5 =
    variableIsInitialized.begin();
  for (; it5 != variableIsInitialized.end(); it5++) {
    SgVarRefExp* varref = it5->first;
    std::pair<SgInitializedName*,bool> p = it5->second;
    SgInitializedName* init = p.first;
    bool ismalloc = p.second;
    ROSE_ASSERT(varref);
    //cerr << "      varInit : " << varref->unparseToString() <<
    //  "    malloc: " << ismalloc << endl;
    insertInitializeVariable(init, varref,ismalloc);
  }

  cerr << "\n Number of Elements in create_array_define_varRef_multiArray  : "
       << create_array_define_varRef_multiArray.size() << endl;
  std::map<SgVarRefExp*, RTedArray*>::const_iterator itm =
    create_array_define_varRef_multiArray.begin();
  for (; itm != create_array_define_varRef_multiArray.end(); itm++) {
    SgVarRefExp* array_node = itm->first;
    RTedArray* array_size = itm->second;
    //cerr << ">>> INserting array create (VARREF): "
    //		<< array_node->unparseToString() << "  size : "
    //		<< array_size->unparseToString() << endl;
    insertArrayCreateCall(array_node, array_size);
  }

  cerr << "\n Number of Elements in variable_declarations  : "
       << variable_declarations.size() << endl;
  std::vector<SgInitializedName*>::const_iterator it1 =
    variable_declarations.begin();
  for (; it1 != variable_declarations.end(); it1++) {
    SgInitializedName* node = *it1;
    insertVariableCreateCall(node);
  }

  cerr << "\n Number of Elements in variable_access  : "
       << variable_access.size() << endl;
  std::vector<SgVarRefExp*>::const_iterator itAccess =
    variable_access.begin();
  for (; itAccess != variable_access.end(); itAccess++) {
    SgVarRefExp* node = *itAccess;
    insertAccessVariable(node);
  }

  cerr
    << "\n Number of Elements in create_array_define_varRef_multiArray_stack  : "
    << create_array_define_varRef_multiArray_stack.size() << endl;
  std::map<SgInitializedName*, RTedArray*>::const_iterator itv =
    create_array_define_varRef_multiArray_stack.begin();
  for (; itv != create_array_define_varRef_multiArray_stack.end(); itv++) {
    SgInitializedName* array_node = itv->first;
    RTedArray* array_size = itv->second;
    //cerr << ">>> INserting array create (VARREF): "
    //		<< array_node->unparseToString() << "  size : "
    //		<< array_size->unparseToString() << endl;
    insertArrayCreateCall(array_node, array_size);
  }

  cerr << "\n Number of Elements in create_array_access_call  : "
       << create_array_access_call.size() << endl;
  std::map<SgVarRefExp*, RTedArray*>::const_iterator ita =
    create_array_access_call.begin();
  for (; ita != create_array_access_call.end(); ita++) {
    SgVarRefExp* array_node = ita->first;
    RTedArray* array_size = ita->second;
    insertArrayAccessCall(array_node, array_size);
  }

  cerr
    << "\n Number of Elements in function_definitions  : "
    << function_definitions.size() << endl;
  BOOST_FOREACH( SgFunctionDefinition* fndef, function_definitions) {
    insertVariableCreateInitForParams( fndef);
  }

  cerr << "\n Number of Elements in funccall_call  : "
       << function_call.size() << endl;
  std::vector<RtedArguments*>::const_iterator it4 =
    function_call.begin();
  for (; it4 != function_call.end(); it4++) {
    RtedArguments* funcs = *it4;
    if (isStringModifyingFunctionCall(funcs->f_name) ) {
      //cerr << " .... Inserting Function Call : " << name << endl;
      insertFuncCall(funcs);
    } else if (isFileIOFunctionCall(funcs->f_name)) {
      insertFuncCall(funcs);
    } else if (isFunctionCallOnIgnoreList(funcs->f_name)) {
      // dont do anything.
    } else {
      // add other internal function calls, such as push variable on stack
      insertStackCall(funcs);
    }
  }

  // insert main call to ->close();
  ROSE_ASSERT(mainLast);
  insertMainCloseCall(mainLast);
}



/* -----------------------------------------------------------
 * Collects information needed for transformations
 * -----------------------------------------------------------*/

void RtedTransformation::visit(SgNode* n) {

  // find MAIN ******************************************
  if (isSgFunctionDefinition(n)) {
    visit_isFunctionDefinition(n);
  }
  // find MAIN ******************************************


  // ******************** DETECT functions in input program  *********************************************************************

  // *********************** DETECT variable creations ***************
  if (isSgVariableDeclaration(n)) {

    // don't track members of user types (structs, classes)
    if( !isSgClassDefinition( n->get_parent()))
      visit_isSgVariableDeclaration(n);
  }

  // *********************** DETECT variable creations ***************




  // *********************** DETECT ALL array creations ***************
  else if (isSgInitializedName(n)) {
    //cerr <<" >> VISITOR :: Found initName : " << n->unparseToString() << endl;
    visit_isArraySgInitializedName(n);
  }

  // 1. look for MALLOC 
  // 2. Look for assignments to variables - i.e. a variable is initialized
  // 3. Assign variables that come from assign initializers (not just assignments
  else if (isSgAssignOp(n)) {
    //cerr <<" >> VISITOR :: Found AssignOp : " << n->unparseToString() << endl;
    visit_isArraySgAssignOp(n);
  }
  else if (isSgAssignInitializer(n)) {
    visit_isAssignInitializer(n);
  }
  // *********************** DETECT ALL array creations ***************


  // *********************** DETECT ALL array accesses ***************
  else if (isSgPntrArrRefExp(n)) {
    // checks for array access
    visit_isArrayPntrArrRefExp(n);
  } // pntrarrrefexp
  else if (isSgVarRefExp(n) && 
	   (isSgExprListExp(isSgVarRefExp(n)->get_parent()) ||
	    isSgExprListExp(isSgVarRefExp(n)->get_parent()->get_parent()))  ) {
    // handles calls to functions that contain array varRefExp
    // and puts the varRefExp on stack to be used by RuntimeSystem
    visit_isArrayExprListExp(n);
  } else if (isSgVarRefExp(n)) {
    // if this is a varrefexp and it is not initialized, we flag it.
    // do only if it is by itself or on right hand side of assignment
    visit_isSgVarRefExp(isSgVarRefExp(n));
  }
  // *********************** DETECT ALL array accesses ***************


  // *********************** DETECT ALL scope statements ***************
  else if (isSgScopeStatement(n)) {
    // if, while, do, etc., where we need to check for locals going out of scope
    visit_isSgScopeStatement(n);
  }
  // *********************** DETECT ALL scope statements ***************



  // *********************** DETECT ALL function calls ***************
  else if (isSgFunctionCallExp(n)) {
    // call to a specific function that needs to be checked
    visit_isFunctionCall(n);
  }
  // *********************** DETECT ALL function calls ***************

  // ******************** DETECT functions in input program  *********************************************************************

}

// vim:et sta ts=2 sw=2:
