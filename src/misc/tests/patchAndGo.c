/*  allInOne.c  */

#include "../../misc.h"
#include "../../FrontMtx.h"
#include "../../SymbFac.h"

/*--------------------------------------------------------------------*/
int
main ( int argc, char *argv[] ) {
/*
   --------------------------------------------------
   all-in-one program to solve A X = B
   We use a patch-and-go strategy 
   for the factorization without pivoting
   (1) read in matrix entries and form DInpMtx object
   (2) form Graph object
   (3) order matrix and form front tree
   (4) get the permutation, permute the matrix and 
       front tree and get the symbolic factorization
   (5) compute the numeric factorization
   (6) read in right hand side entries
   (7) compute the solution

   created -- 98jun04, cca
   --------------------------------------------------
*/
/*--------------------------------------------------------------------*/
char            *matrixFileName, *rhsFileName ;
DenseMtx        *mtxB, *mtxX ;
double          fudge, toosmall ;
Chv             *rootchv ;
ChvManager      *chvmanager  ;
SubMtxManager   *mtxmanager  ;
FrontMtx        *frontmtx ;
InpMtx          *mtxA ;
double          imag, real, tau = 100., value ;
double          cpus[10] ;
ETree           *frontETree ;
FILE            *inputFile, *msgFile ;
Graph           *graph ;
int             error, ient, irow, jcol, jrhs, jrow, msglvl, ncol, 
                nedges, nent, neqns, nrhs, nrow, patchAndGoFlag,
                seed, storeids, storevalues, symmetryflag, type ;
int             *newToOld, *oldToNew ;
int             stats[20] ;
IV              *newToOldIV, *oldToNewIV ;
IVL             *adjIVL, *symbfacIVL ;
/*--------------------------------------------------------------------*/
/*
   --------------------
   get input parameters
   --------------------
*/
if ( argc != 13 ) {
   fprintf(stdout, "\n"
      "\n usage: %s msglvl msgFile type symmetryflag "
      "\n        patchAndGoFlag fudge toosmall storeids storevalues"
      "\n        matrixFileName rhsFileName seed"
      "\n    msglvl -- message level"
      "\n    msgFile -- message file"
      "\n    type    -- type of entries"
      "\n      1 (SPOOLES_REAL)    -- real entries"
      "\n      2 (SPOOLES_COMPLEX) -- complex entries"
      "\n    symmetryflag -- type of matrix"
      "\n      0 (SPOOLES_SYMMETRIC)    -- symmetric entries"
      "\n      1 (SPOOLES_HERMITIAN)    -- Hermitian entries"
      "\n      2 (SPOOLES_NONSYMMETRIC) -- nonsymmetric entries"
      "\n    matrixFileName -- matrix file name, format"
      "\n       nrow ncol nent"
      "\n       irow jcol entry"
      "\n        ..."
      "\n        note: indices are zero based"
      "\n    rhsFileName -- right hand side file name, format"
      "\n       nrow nrhs "
      "\n       ..."
      "\n       jrow entry(jrow,0) ... entry(jrow,nrhs-1)"
      "\n       ..."
      "\n    seed -- random number seed, used for ordering"
      "\n", argv[0]) ;
   return(0) ;
}
msglvl = atoi(argv[1]) ;
if ( strcmp(argv[2], "stdout") == 0 ) {
   msgFile = stdout ;
} else if ( (msgFile = fopen(argv[2], "a")) == NULL ) {
   fprintf(stderr, "\n fatal error in %s"
           "\n unable to open file %s\n",
           argv[0], argv[2]) ;
   return(-1) ;
}
type           = atoi(argv[3]) ;
symmetryflag   = atoi(argv[4]) ;
patchAndGoFlag = atoi(argv[5]) ;
fudge          = atof(argv[6]) ;
toosmall       = atof(argv[7]) ;
storeids       = atoi(argv[8]) ;
storevalues    = atoi(argv[9]) ;
matrixFileName = argv[10] ;
rhsFileName    = argv[11] ;
seed           = atoi(argv[12]) ;
/*--------------------------------------------------------------------*/
/*
   --------------------------------------------
   STEP 1: read the entries from the input file 
           and create the InpMtx object
   --------------------------------------------
*/
inputFile = fopen(matrixFileName, "r") ;
fscanf(inputFile, "%d %d %d", &nrow, &ncol, &nent) ;
neqns = nrow ;
mtxA = InpMtx_new() ;
InpMtx_init(mtxA, INPMTX_BY_ROWS, type, nent, 0) ;
if ( type == SPOOLES_REAL ) {
   for ( ient = 0 ; ient < nent ; ient++ ) {
      fscanf(inputFile, "%d %d %le", &irow, &jcol, &value) ;
      InpMtx_inputRealEntry(mtxA, irow, jcol, value) ;
   }
} else {
   for ( ient = 0 ; ient < nent ; ient++ ) {
      fscanf(inputFile, "%d %d %le %le", &irow, &jcol, &real, &imag) ;
      InpMtx_inputComplexEntry(mtxA, irow, jcol, real, imag) ;
   }
}
fclose(inputFile) ;
InpMtx_changeStorageMode(mtxA, INPMTX_BY_VECTORS) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n input matrix") ;
   InpMtx_writeForHumanEye(mtxA, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   -------------------------------------------------
   STEP 2 : find a low-fill ordering
   (1) create the Graph object
   (2) order the graph using multiple minimum degree
   -------------------------------------------------
*/
graph = Graph_new() ;
adjIVL = InpMtx_fullAdjacency(mtxA) ;
nedges = IVL_tsize(adjIVL) ;
Graph_init2(graph, 0, neqns, 0, nedges, neqns, nedges, adjIVL,
            NULL, NULL) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n graph of the input matrix") ;
   Graph_writeForHumanEye(graph, msgFile) ;
   fflush(msgFile) ;
}
frontETree = orderViaMMD(graph, seed, msglvl, msgFile) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n front tree from ordering") ;
   ETree_writeForHumanEye(frontETree, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   -----------------------------------------------------
   STEP 3: get the permutation, permute the matrix and 
           front tree and get the symbolic factorization
   -----------------------------------------------------
*/
oldToNewIV = ETree_oldToNewVtxPerm(frontETree) ;
oldToNew = IV_entries(oldToNewIV) ;
newToOldIV = ETree_newToOldVtxPerm(frontETree) ;
newToOld   = IV_entries(newToOldIV) ;
ETree_permuteVertices(frontETree, oldToNewIV) ;
InpMtx_permute(mtxA, oldToNew, oldToNew) ;
InpMtx_mapToUpperTriangle(mtxA) ;
InpMtx_changeCoordType(mtxA, INPMTX_BY_CHEVRONS) ;
InpMtx_changeStorageMode(mtxA, INPMTX_BY_VECTORS) ;
symbfacIVL = SymbFac_initFromInpMtx(frontETree, mtxA) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n old-to-new permutation vector") ;
   IV_writeForHumanEye(oldToNewIV, msgFile) ;
   fprintf(msgFile, "\n\n new-to-old permutation vector") ;
   IV_writeForHumanEye(newToOldIV, msgFile) ;
   fprintf(msgFile, "\n\n front tree after permutation") ;
   ETree_writeForHumanEye(frontETree, msgFile) ;
   fprintf(msgFile, "\n\n input matrix after permutation") ;
   InpMtx_writeForHumanEye(mtxA, msgFile) ;
   fprintf(msgFile, "\n\n symbolic factorization") ;
   IVL_writeForHumanEye(symbfacIVL, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   ------------------------------------------
   STEP 4: initialize the front matrix object
   ------------------------------------------
*/
frontmtx = FrontMtx_new() ;
mtxmanager = SubMtxManager_new() ;
SubMtxManager_init(mtxmanager, NO_LOCK, 0) ;
FrontMtx_init(frontmtx, frontETree, symbfacIVL, type, symmetryflag, 
              FRONTMTX_DENSE_FRONTS, SPOOLES_NO_PIVOTING, NO_LOCK, 
              0, NULL, mtxmanager, msglvl, msgFile) ;
if ( patchAndGoFlag == 1 ) {
   frontmtx->patchinfo = PatchAndGoInfo_new() ;
   PatchAndGoInfo_init(frontmtx->patchinfo, 1, toosmall, fudge,
                       storeids, storevalues) ;
} else if ( patchAndGoFlag == 2 ) {
   frontmtx->patchinfo = PatchAndGoInfo_new() ;
   PatchAndGoInfo_init(frontmtx->patchinfo, 2, toosmall, fudge,
                       storeids, storevalues) ;
}
/*--------------------------------------------------------------------*/
/*
   -----------------------------------------
   STEP 5: compute the numeric factorization
   -----------------------------------------
*/
chvmanager = ChvManager_new() ;
ChvManager_init(chvmanager, NO_LOCK, 1) ;
DVfill(10, cpus, 0.0) ;
IVfill(20, stats, 0) ;
rootchv = FrontMtx_factorInpMtx(frontmtx, mtxA, tau, 0.0, chvmanager,
                                &error, cpus, stats, msglvl, msgFile) ;
ChvManager_free(chvmanager) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n factor matrix") ;
   FrontMtx_writeForHumanEye(frontmtx, msgFile) ;
   fflush(msgFile) ;
}
if ( patchAndGoFlag == 1 ) {
   if ( frontmtx->patchinfo->fudgeIV != NULL ) {
      fprintf(msgFile, "\n small pivots found at these locations") ;
      IV_writeForHumanEye(frontmtx->patchinfo->fudgeIV, msgFile) ;
   }
   PatchAndGoInfo_free(frontmtx->patchinfo) ;
} else if ( patchAndGoFlag == 2 ) {
   if ( frontmtx->patchinfo->fudgeIV != NULL ) {
      fprintf(msgFile, "\n small pivots found at these locations") ;
      IV_writeForHumanEye(frontmtx->patchinfo->fudgeIV, msgFile) ;
   }
   if ( frontmtx->patchinfo->fudgeDV != NULL ) {
      fprintf(msgFile, "\n perturbations") ;
      DV_writeForHumanEye(frontmtx->patchinfo->fudgeDV, msgFile) ;
   }
   PatchAndGoInfo_free(frontmtx->patchinfo) ;
}
if ( rootchv != NULL ) {
   fprintf(msgFile, "\n\n matrix found to be singular\n") ;
   exit(-1) ;
}
if ( error >= 0 ) {
   fprintf(msgFile, "\n\n error encountered at front %d", error) ;
   exit(-1) ;
}
/*--------------------------------------------------------------------*/
/*
   --------------------------------------
   STEP 6: post-process the factorization
   --------------------------------------
*/
FrontMtx_postProcess(frontmtx, msglvl, msgFile) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n factor matrix after post-processing") ;
   FrontMtx_writeForHumanEye(frontmtx, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   -----------------------------------------
   STEP 7: read the right hand side matrix B
   -----------------------------------------
*/
inputFile = fopen(rhsFileName, "r") ;
fscanf(inputFile, "%d %d", &nrow, &nrhs) ;
mtxB = DenseMtx_new() ;
DenseMtx_init(mtxB, type, 0, 0, neqns, nrhs, 1, neqns) ;
DenseMtx_zero(mtxB) ;
if ( type == SPOOLES_REAL ) {
   for ( irow = 0 ; irow < nrow ; irow++ ) {
      fscanf(inputFile, "%d", &jrow) ;
      for ( jrhs = 0 ; jrhs < nrhs ; jrhs++ ) {
         fscanf(inputFile, "%le", &value) ;
         DenseMtx_setRealEntry(mtxB, jrow, jrhs, value) ;
      }
   }
} else {
   for ( irow = 0 ; irow < nrow ; irow++ ) {
      fscanf(inputFile, "%d", &jrow) ;
      for ( jrhs = 0 ; jrhs < nrhs ; jrhs++ ) {
         fscanf(inputFile, "%le %le", &real, &imag) ;
         DenseMtx_setComplexEntry(mtxB, jrow, jrhs, real, imag) ;
      }
   }
}
fclose(inputFile) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n rhs matrix in original ordering") ;
   DenseMtx_writeForHumanEye(mtxB, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   ---------------------------------------------------------
   STEP 8: permute the right hand side into the new ordering
   ---------------------------------------------------------
*/
DenseMtx_permuteRows(mtxB, oldToNewIV) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n right hand side matrix in new ordering") ;
   DenseMtx_writeForHumanEye(mtxB, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   -------------------------------
   STEP 9: solve the linear system
   -------------------------------
*/
mtxX = DenseMtx_new() ;
DenseMtx_init(mtxX, type, 0, 0, neqns, nrhs, 1, neqns) ;
DenseMtx_zero(mtxX) ;
FrontMtx_solve(frontmtx, mtxX, mtxB, mtxmanager, 
               cpus, msglvl, msgFile) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n solution matrix in new ordering") ;
   DenseMtx_writeForHumanEye(mtxX, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   --------------------------------------------------------
   STEP 10: permute the solution into the original ordering
   --------------------------------------------------------
*/
DenseMtx_permuteRows(mtxX, newToOldIV) ;
if ( msglvl > 1 ) {
   fprintf(msgFile, "\n\n solution matrix in original ordering") ;
   DenseMtx_writeForHumanEye(mtxX, msgFile) ;
   fflush(msgFile) ;
}
/*--------------------------------------------------------------------*/
/*
   -----------
   free memory
   -----------
*/
FrontMtx_free(frontmtx) ;
DenseMtx_free(mtxX) ;
DenseMtx_free(mtxB) ;
IV_free(newToOldIV) ;
IV_free(oldToNewIV) ;
InpMtx_free(mtxA) ;
ETree_free(frontETree) ;
IVL_free(symbfacIVL) ;
SubMtxManager_free(mtxmanager) ;
Graph_free(graph) ;
/*--------------------------------------------------------------------*/
return(1) ; }
/*--------------------------------------------------------------------*/
