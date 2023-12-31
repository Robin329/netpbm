/*============================================================================ 
                                pamcut
==============================================================================
  Cut a rectangle out of a Netpbm image

  This is inspired by and intended as a replacement for Pnmcut by 
  Jef Poskanzer, 1989.

  By Bryan Henderson, San Jose CA.  Contributed to the public domain
  by its author.
============================================================================*/

#include <limits.h>
#include <assert.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"

#define UNSPEC INT_MAX
    /* UNSPEC is the value we use for an argument that is not specified
       by the user.  Theoretically, the user could specify this value,
       but we hope not.
       */

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* File name of input file */

    /* The following describe the rectangle the user wants to cut out. 
       the value UNSPEC for any of them indicates that value was not
       specified.  A negative value means relative to the far edge.
       'width' and 'height' are not negative.  These specifications 
       do not necessarily describe a valid rectangle; they are just
       what the user said.
       */
    int left;
    int right;
    int top;
    int bottom;
    int width;
    int height;
    unsigned int pad;

    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "left",       OPT_INT,    &cmdlineP->left,     NULL,      0);
    OPTENT3(0,   "right",      OPT_INT,    &cmdlineP->right,    NULL,      0);
    OPTENT3(0,   "top",        OPT_INT,    &cmdlineP->top,      NULL,      0);
    OPTENT3(0,   "bottom",     OPT_INT,    &cmdlineP->bottom,   NULL,      0);
    OPTENT3(0,   "width",      OPT_INT,    &cmdlineP->width,    NULL,      0);
    OPTENT3(0,   "height",     OPT_INT,    &cmdlineP->height,   NULL,      0);
    OPTENT3(0,   "pad",        OPT_FLAG,   NULL, &cmdlineP->pad,           0);
    OPTENT3(0,   "verbose",    OPT_FLAG,   NULL, &cmdlineP->verbose,       0);

    /* Set the defaults */
    cmdlineP->left = UNSPEC;
    cmdlineP->right = UNSPEC;
    cmdlineP->top = UNSPEC;
    cmdlineP->bottom = UNSPEC;
    cmdlineP->width = UNSPEC;
    cmdlineP->height = UNSPEC;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = TRUE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->width < 0)
        pm_error("-width may not be negative.");
    if (cmdlineP->height < 0)
        pm_error("-height may not be negative.");

    if ((argc-1) != 0 && (argc-1) != 1 && (argc-1) != 4 && (argc-1) != 5)
        pm_error("Wrong number of arguments: %u.  The only argument in "
                 "the preferred syntax is an optional input file name.  "
                 "In older syntax, there are also forms with 4 and 5 "
                 "arguments.", argc-1);

    switch (argc-1) {
    case 0:
        cmdlineP->inputFileName = "-";
        break;
    case 1:
        cmdlineP->inputFileName = argv[1];
        break;
    case 4:
    case 5: {
        int warg, harg;  /* The "width" and "height" command line arguments */

        if (sscanf(argv[1], "%d", &cmdlineP->left) != 1)
            pm_error("Invalid number for left column argument");
        if (sscanf(argv[2], "%d", &cmdlineP->top) != 1)
            pm_error("Invalid number for right column argument");
        if (sscanf(argv[3], "%d", &warg) != 1)
            pm_error("Invalid number for width argument");
        if (sscanf(argv[4], "%d", &harg) != 1)
            pm_error("Invalid number for height argument");

        if (warg > 0) {
            cmdlineP->width = warg;
            cmdlineP->right = UNSPEC;
        } else {
            cmdlineP->width = UNSPEC;
            cmdlineP->right = warg -1;
        }
        if (harg > 0) {
            cmdlineP->height = harg;
            cmdlineP->bottom = UNSPEC;
        } else {
            cmdlineP->height = UNSPEC;
            cmdlineP->bottom = harg - 1;
        }

        if (argc-1 == 4)
            cmdlineP->inputFileName = "-";
        else
            cmdlineP->inputFileName = argv[5];
        break;
    }
    }
}



static void
computeCutBounds(const int cols, const int rows,
                 const int leftarg, const int rightarg, 
                 const int toparg, const int bottomarg,
                 const int widtharg, const int heightarg,
                 int * const leftcolP, int * const rightcolP,
                 int * const toprowP, int * const bottomrowP) {
/*----------------------------------------------------------------------------
   From the values given on the command line 'leftarg', 'rightarg',
   'toparg', 'bottomarg', 'widtharg', and 'heightarg', determine what
   rectangle the user wants cut out.

   Any of these arguments may be UNSPEC to indicate "not specified".
   Any except 'widtharg' and 'heightarg' may be negative to indicate
   relative to the far edge.  'widtharg' and 'heightarg' are positive.

   Return the location of the rectangle as *leftcolP, *rightcolP,
   *toprowP, and *bottomrowP.  
-----------------------------------------------------------------------------*/

    int leftcol, rightcol, toprow, bottomrow;
        /* The left and right column numbers and top and bottom row numbers
           specified by the user, except with negative values translated
           into the actual values.

           Note that these may very well be negative themselves, such
           as when the user says "column -10" and there are only 5 columns
           in the image.
           */

    /* Translate negative column and row into real column and row */
    /* Exploit the fact that UNSPEC is a positive number */

    if (leftarg >= 0)
        leftcol = leftarg;
    else
        leftcol = cols + leftarg;
    if (rightarg >= 0)
        rightcol = rightarg;
    else
        rightcol = cols + rightarg;
    if (toparg >= 0)
        toprow = toparg;
    else
        toprow = rows + toparg;
    if (bottomarg >= 0)
        bottomrow = bottomarg;
    else
        bottomrow = rows + bottomarg;

    /* Sort out left, right, and width specifications */

    if (leftcol == UNSPEC && rightcol == UNSPEC && widtharg == UNSPEC) {
        *leftcolP = 0;
        *rightcolP = cols - 1;
    }
    if (leftcol == UNSPEC && rightcol == UNSPEC && widtharg != UNSPEC) {
        *leftcolP = 0;
        *rightcolP = 0 + widtharg - 1;
    }
    if (leftcol == UNSPEC && rightcol != UNSPEC && widtharg == UNSPEC) {
        *leftcolP = 0;
        *rightcolP = rightcol;
    }
    if (leftcol == UNSPEC && rightcol != UNSPEC && widtharg != UNSPEC) {
        *leftcolP = rightcol - widtharg + 1;
        *rightcolP = rightcol;
    }
    if (leftcol != UNSPEC && rightcol == UNSPEC && widtharg == UNSPEC) {
        *leftcolP = leftcol;
        *rightcolP = cols - 1;
    }
    if (leftcol != UNSPEC && rightcol == UNSPEC && widtharg != UNSPEC) {
        *leftcolP = leftcol;
        *rightcolP = leftcol + widtharg - 1;
    }
    if (leftcol != UNSPEC && rightcol != UNSPEC && widtharg == UNSPEC) {
        *leftcolP = leftcol;
        *rightcolP = rightcol;
    }
    if (leftcol != UNSPEC && rightcol != UNSPEC && widtharg != UNSPEC) {
        pm_error("You may not specify left, right, and width.\n"
                 "Choose at most two of these.");
    }


    /* Sort out top, bottom, and height specifications */

    if (toprow == UNSPEC && bottomrow == UNSPEC && heightarg == UNSPEC) {
        *toprowP = 0;
        *bottomrowP = rows - 1;
    }
    if (toprow == UNSPEC && bottomrow == UNSPEC && heightarg != UNSPEC) {
        *toprowP = 0;
        *bottomrowP = 0 + heightarg - 1;
    }
    if (toprow == UNSPEC && bottomrow != UNSPEC && heightarg == UNSPEC) {
        *toprowP = 0;
        *bottomrowP = bottomrow;
    }
    if (toprow == UNSPEC && bottomrow != UNSPEC && heightarg != UNSPEC) {
        *toprowP = bottomrow - heightarg + 1;
        *bottomrowP = bottomrow;
    }
    if (toprow != UNSPEC && bottomrow == UNSPEC && heightarg == UNSPEC) {
        *toprowP = toprow;
        *bottomrowP = rows - 1;
    }
    if (toprow != UNSPEC && bottomrow == UNSPEC && heightarg != UNSPEC) {
        *toprowP = toprow;
        *bottomrowP = toprow + heightarg - 1;
    }
    if (toprow != UNSPEC && bottomrow != UNSPEC && heightarg == UNSPEC) {
        *toprowP = toprow;
        *bottomrowP = bottomrow;
    }
    if (toprow != UNSPEC && bottomrow != UNSPEC && heightarg != UNSPEC) {
        pm_error("You may not specify top, bottom, and height.\n"
                 "Choose at most two of these.");
    }

}



static void
rejectOutOfBounds(unsigned int const cols,
                  unsigned int const rows,
                  int          const leftcol,
                  int          const rightcol,
                  int          const toprow,
                  int          const bottomrow,
                  bool         const pad) {

     /* Reject coordinates off the edge */

    if (!pad) {
        if (leftcol < 0)
            pm_error("You have specified a left edge (%d) that is beyond "
                     "the left edge of the image (0)", leftcol);
        if (leftcol > (int)(cols-1))
            pm_error("You have specified a left edge (%d) that is beyond "
                     "the right edge of the image (%u)", leftcol, cols-1);
        if (rightcol < 0)
            pm_error("You have specified a right edge (%d) that is beyond "
                     "the left edge of the image (0)", rightcol);
        if (rightcol > (int)(cols-1))
            pm_error("You have specified a right edge (%d) that is beyond "
                     "the right edge of the image (%u)", rightcol, cols-1);
        if (toprow < 0)
            pm_error("You have specified a top edge (%d) that is above "
                     "the top edge of the image (0)", toprow);
        if (toprow > (int)(rows-1))
            pm_error("You have specified a top edge (%d) that is below "
                     "the bottom edge of the image (%u)", toprow, rows-1);
        if (bottomrow < 0)
            pm_error("You have specified a bottom edge (%d) that is above "
                     "the top edge of the image (0)", bottomrow);
        if (bottomrow > (int)(rows-1))
            pm_error("You have specified a bottom edge (%d) that is below "
                     "the bottom edge of the image (%u)", bottomrow, rows-1);
    }

    if (leftcol > rightcol)
        pm_error("You have specified a left edge (%d) that is to the right of "
                 "the right edge you specified (%d)",
                 leftcol, rightcol);

    if (toprow > bottomrow)
        pm_error("You have specified a top edge (%d) that is below "
                 "the bottom edge you specified (%d)",
                 toprow, bottomrow);
}



static void
writeBlackRows(const struct pam * const outpamP, 
               int                const rows) {
/*----------------------------------------------------------------------------
   Write out 'rows' rows of black tuples of the image described by *outpamP.

   Unless our input image is PBM, PGM, or PPM, or PAM equivalent, we
   don't really know what "black" means, so this is just something
   arbitrary in that case.
-----------------------------------------------------------------------------*/
    tuple blackTuple;
    tuple * blackRow;
    int col;
    
    pnm_createBlackTuple(outpamP, &blackTuple);

    MALLOCARRAY_NOFAIL(blackRow, outpamP->width);
    
    for (col = 0; col < outpamP->width; ++col)
        blackRow[col] = blackTuple;

    pnm_writepamrowmult(outpamP, blackRow, rows);

    free(blackRow);

    pnm_freepamtuple(blackTuple);
}



struct rowCutter {
/*----------------------------------------------------------------------------
   This is an object that gives you pointers you can use to effect the
   horizontal cutting and padding of a row just by doing one
   pnm_readpamrow() and one pnm_writepamrow().  It works like this:

   The array inputPointers[] contains an element for each pixel in an input
   row.  If it's a pixel that gets discarded in the cutting process, 
   inputPointers[] points to a special "discard" tuple.  All thrown away
   pixels have the same discard tuple to save CPU cache space.  If it's
   a pixel that gets copied to the output, inputPointers[] points to some
   tuple to which outputPointers[] also points.

   The array outputPointers[] contains an element for each pixel in an
   output row.  If the pixel is one that gets copied from the input, 
   outputPointers[] points to some tuple to which inputPointers[] also
   points.  If it's a pixel that gets padded with black, outputPointers[]
   points to a constant black tuple.  All padded pixels have the same
   constant black tuple to save CPU cache space.

   For example, if you have a three pixel input row and are cutting
   off the right two pixels, inputPointers[0] points to copyTuples[0]
   and inputPointers[1] and inputPointers[2] point to discardTuple.
   outputPointers[0] points to copyTuples[0].

   We arrange to have the padded parts of the output row filled with
   black tuples.  Unless the input image is PBM, PGM, or PPM, or PAM
   equivalent, we don't really know what "black" means, so we fill with
   something arbitrary in that case.
-----------------------------------------------------------------------------*/
    tuple * inputPointers;
    tuple * outputPointers;

    unsigned int inputWidth;
    unsigned int outputWidth;

    /* The following are the tuples to which inputPointers[] and
       outputPointers[] may point.
    */
    tuple * copyTuples;
    tuple blackTuple;
    tuple discardTuple;
};



/* In a typical multi-image stream, all the images have the same
   dimensions, so this program creates and destroys identical row
   cutters for each image in the stream.  If that turns out to take a
   significant amount of resource to do, we should create a cache:
   keep the last row cutter made, tagged by the parameters used to
   create it.  If the parameters are the same for the next image, we
   just use that cached row cutter; otherwise, we discard it and
   create a new one then.
*/



static void
createRowCutter(const struct pam *  const inpamP,
                const struct pam *  const outpamP,
                int                 const leftcol,
                int                 const rightcol,
                struct rowCutter ** const rowCutterPP) {

    struct rowCutter * rowCutterP;
    tuple * inputPointers;
    tuple * outputPointers;
    tuple * copyTuples;
    tuple blackTuple;
    tuple discardTuple;
    int col;

    assert(inpamP->depth >= outpamP->depth);
        /* Entry condition.  If this weren't true, we could not simply
           treat an input tuple as an output tuple.
        */

    copyTuples   = pnm_allocpamrow(outpamP);
    discardTuple = pnm_allocpamtuple(inpamP);
    pnm_createBlackTuple(outpamP, &blackTuple);

    MALLOCARRAY_NOFAIL(inputPointers,  inpamP->width);
    MALLOCARRAY_NOFAIL(outputPointers, outpamP->width);

    /* Put in left padding */
    for (col = leftcol; col < 0 && col-leftcol < outpamP->width; ++col)
        outputPointers[col-leftcol] = blackTuple;

    /* Put in extracted columns */
    for (col = MAX(leftcol, 0);
         col <= MIN(rightcol, inpamP->width-1);
         ++col) {
        int const outcol = col - leftcol;

        inputPointers[col] = outputPointers[outcol] = copyTuples[outcol];
    }

    /* Put in right padding */
    for (col = MIN(rightcol, inpamP->width-1) + 1; col <= rightcol; ++col) {
        if (col - leftcol >= 0) {
            outputPointers[col-leftcol] = blackTuple;
        }
    }

    /* Direct input pixels that are getting cut off to the discard tuple */

    for (col = 0; col < MIN(leftcol, inpamP->width); ++col)
        inputPointers[col] = discardTuple;

    for (col = MAX(0, rightcol + 1); col < inpamP->width; ++col)
        inputPointers[col] = discardTuple;

    MALLOCVAR_NOFAIL(rowCutterP);

    rowCutterP->inputWidth     = inpamP->width;
    rowCutterP->outputWidth    = outpamP->width;
    rowCutterP->inputPointers  = inputPointers;
    rowCutterP->outputPointers = outputPointers;
    rowCutterP->copyTuples     = copyTuples;
    rowCutterP->discardTuple   = discardTuple;
    rowCutterP->blackTuple     = blackTuple;

    *rowCutterPP = rowCutterP;
}



static void
destroyRowCutter(struct rowCutter * const rowCutterP) {

    pnm_freepamrow(rowCutterP->copyTuples);
    pnm_freepamtuple(rowCutterP->blackTuple);
    pnm_freepamtuple(rowCutterP->discardTuple);
    free(rowCutterP->inputPointers);
    free(rowCutterP->outputPointers);
    
    free(rowCutterP);
}



static void
extractRowsGen(const struct pam * const inpamP,
               const struct pam * const outpamP,
               int                const leftcol,
               int                const rightcol,
               int                const toprow,
               int                const bottomrow) {

    struct rowCutter * rowCutterP;
    int row;

    /* Write out top padding */
    if (0 - toprow > 0)
        writeBlackRows(outpamP, 0 - toprow);

    createRowCutter(inpamP, outpamP, leftcol, rightcol, &rowCutterP);

    /* Read input and write out rows extracted from it */
    for (row = 0; row < inpamP->height; ++row) {
        if (row >= toprow && row <= bottomrow){
            pnm_readpamrow(inpamP, rowCutterP->inputPointers);
            pnm_writepamrow(outpamP, rowCutterP->outputPointers);
        } else  /* row < toprow || row > bottomrow */
            pnm_readpamrow(inpamP, NULL);
        
        /* Note that we may be tempted just to quit after reaching the bottom
           of the extracted image, but that would cause a broken pipe problem
           for the process that's feeding us the image.
        */
    }

    destroyRowCutter(rowCutterP);
    
    /* Write out bottom padding */
    if ((bottomrow - (inpamP->height-1)) > 0)
        writeBlackRows(outpamP, bottomrow - (inpamP->height-1));
}



static void
makeBlackPBMRow(unsigned char * const bitrow,
                unsigned int    const cols) {

    unsigned int const colByteCnt = pbm_packed_bytes(cols);

    unsigned int i;

    for (i = 0; i < colByteCnt; ++i)
        bitrow[i] = PBM_BLACK * 0xff;

    if (PBM_BLACK != 0 && cols % 8 > 0)
        bitrow[colByteCnt-1] <<= (8 - cols % 8);
}



static void
extractRowsPBM(const struct pam * const inpamP,
               const struct pam * const outpamP,
               int                const leftcol,
               int                const rightcol,
               int                const toprow,
               int                const bottomrow) {

    unsigned char * bitrow;
    int             readOffset, writeOffset;
    int             row;
    unsigned int    totalWidth;

    assert(leftcol <= rightcol);
    assert(toprow <= bottomrow);

    if (leftcol > 0) {
        totalWidth = MAX(rightcol+1, inpamP->width) + 7;
        if (totalWidth > INT_MAX - 10)
            /* Prevent overflows in pbm_allocrow_packed() */
            pm_error("Specified right edge is too far "
                     "from the right end of input image");
        
        readOffset  = 0;
        writeOffset = leftcol;
    } else {
        totalWidth = -leftcol + MAX(rightcol+1, inpamP->width);
        if (totalWidth > INT_MAX - 10)
            pm_error("Specified left/right edge is too far "
                     "from the left/right end of input image");
        
        readOffset = -leftcol;
        writeOffset = 0;
    }

    bitrow = pbm_allocrow_packed(totalWidth);

    if (toprow < 0 || leftcol < 0 || rightcol >= inpamP->width){
        makeBlackPBMRow(bitrow, totalWidth);
        if (toprow < 0) {
            int row;
            for (row=0; row < 0 - toprow; ++row)
                pbm_writepbmrow_packed(outpamP->file, bitrow,
                                       outpamP->width, 0);
        }
    }

    for (row = 0; row < inpamP->height; ++row){
        if (row >= toprow && row <= bottomrow) {
            pbm_readpbmrow_bitoffset(inpamP->file, bitrow, inpamP->width,
                                     inpamP->format, readOffset);

            pbm_writepbmrow_bitoffset(outpamP->file, bitrow, outpamP->width,
                                      0, writeOffset);
  
            if (rightcol >= inpamP->width)
                /* repair right padding */
                bitrow[writeOffset/8 + pbm_packed_bytes(outpamP->width) - 1] =
                    0xff * PBM_BLACK;
        } else
            pnm_readpamrow(inpamP, NULL);    /* read and discard */
    }

    if (bottomrow - (inpamP->height-1) > 0) {
        int row;
        makeBlackPBMRow(bitrow, outpamP->width);
        for (row = 0; row < bottomrow - (inpamP->height-1); ++row)
            pbm_writepbmrow_packed(outpamP->file, bitrow, outpamP->width, 0);
    }
    pbm_freerow_packed(bitrow);
}



static void
cutOneImage(FILE *             const ifP,
            struct CmdlineInfo const cmdline,
            FILE *             const ofP) {

    int leftcol, rightcol, toprow, bottomrow;
    struct pam inpam;   /* Input PAM image */
    struct pam outpam;  /* Output PAM image */

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));
    
    computeCutBounds(inpam.width, inpam.height, 
                     cmdline.left, cmdline.right, 
                     cmdline.top, cmdline.bottom, 
                     cmdline.width, cmdline.height, 
                     &leftcol, &rightcol, &toprow, &bottomrow);

    rejectOutOfBounds(inpam.width, inpam.height, leftcol, rightcol, 
                      toprow, bottomrow, cmdline.pad);

    if (cmdline.verbose) {
        pm_message("Image goes from Row 0, Column 0 through Row %u, Column %u",
                   inpam.height-1, inpam.width-1);
        pm_message("Cutting from Row %d, Column %d through Row %d Column %d",
                   toprow, leftcol, bottomrow, rightcol);
    }

    outpam = inpam;    /* Initial value -- most fields should be same */
    outpam.file   = ofP;
    outpam.width  = rightcol - leftcol + 1;
    outpam.height = bottomrow - toprow + 1;

    pnm_writepaminit(&outpam);

    if (PNM_FORMAT_TYPE(outpam.format) == PBM_TYPE)
        extractRowsPBM(&inpam, &outpam, leftcol, rightcol, toprow, bottomrow);
    else
        extractRowsGen(&inpam, &outpam, leftcol, rightcol, toprow, bottomrow);
}



int
main(int argc, const char *argv[]) {

    FILE * const ofP = stdout;

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int eof;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    eof = FALSE;
    while (!eof) {
        cutOneImage(ifP, cmdline, ofP);
        pnm_nextimage(ifP, &eof);
    }

    pm_close(ifP);
    pm_close(ofP);
    
    return 0;
}
