/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

 /*-------------------------------------------------------------------------
 *
 * Created:     vfd_swmr_sparse_writer.c
 *              (copied and modified from swmr_sparse_writer.c)
 *
 * Purpose:     Writes data to a randomly selected subset of the datasets
 *              in the VFD_SWMR test file.
 *
 *              This program is intended to run concurrently with the
 *              vfd_swmr_sparse_reader program.
 *
 *-------------------------------------------------------------------------
 */

/***********/
/* Headers */
/***********/

#include "h5test.h"
#include "vfd_swmr_common.h"

/****************/
/* Local Macros */
/****************/

#ifdef OUT
#define BUSY_WAIT 100000
#endif /* OUT */

/********************/
/* Local Prototypes */
/********************/

static hid_t open_skeleton(const char *filename, unsigned verbose);
static int add_records(hid_t fid, unsigned verbose, unsigned long nrecords,
    unsigned long flush_count);
static void usage(void);



/*-------------------------------------------------------------------------
 * Function:    open_skeleton
 *
 * Purpose:     Opens the SWMR HDF5 file and datasets.
 *
 * Parameters:  const char *filename
 *              The filename of the SWMR HDF5 file to open
 *
 *              unsigned verbose
 *              Whether or not to emit verbose console messages
 *
 * Return:      Success:    The file ID of the opened SWMR file
 *                          The dataset IDs are stored in a global array
 *
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static hid_t
open_skeleton(const char *filename, unsigned verbose)
{
    hid_t fid;          /* File ID for new HDF5 file */
    hid_t fapl;         /* File access property list */
    hid_t aid;          /* Attribute ID */
    unsigned seed;      /* Seed for random number generator */
    unsigned u, v;      /* Local index variable */
    H5F_vfd_swmr_config_t *config = NULL;   /* Configuration for VFD SWMR */

    HDassert(filename);

    /* Create file access property list */
    if((fapl = h5_fileaccess()) < 0)
        return -1;

    /* Set to use the latest library format */
    if(H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) < 0)
        return -1;

#ifdef QAK
    /* Increase the initial size of the metadata cache */
    {
        H5AC_cache_config_t mdc_config;

        mdc_config.version = H5AC__CURR_CACHE_CONFIG_VERSION;
        H5Pget_mdc_config(fapl, &mdc_config);
        HDfprintf(stderr, "mdc_config.initial_size = %lu\n", (unsigned long)mdc_config.initial_size);
        HDfprintf(stderr,"mdc_config.epoch_length = %lu\n", (unsigned long)mdc_config.epoch_length);
        mdc_config.set_initial_size = 1;
        mdc_config.initial_size = 16 * 1024 * 1024;
        /* mdc_config.epoch_length = 5000; */
        H5Pset_mdc_config(fapl, &mdc_config);
    }
#endif /* QAK */

#ifdef QAK
    H5Pset_fapl_log(fapl, "append.log", H5FD_LOG_ALL, (size_t)(512 * 1024 * 1024));
#endif /* QAK */

    /*
     * Set up to open the file with VFD SWMR configured.
     */

    /* Enable page buffering */
    if(H5Pset_page_buffer_size(fapl, 4096, 0, 0) < 0)
        return -1;

     /* Allocate memory for the configuration structure */
     if((config = (H5F_vfd_swmr_config_t *)HDmalloc(sizeof(H5F_vfd_swmr_config_t))) == NULL)
        return -1;

    config->version = H5F__CURR_VFD_SWMR_CONFIG_VERSION;
    config->tick_len = 4;
    config->max_lag = 5;
    config->vfd_swmr_writer = TRUE;
    config->md_pages_reserved = 128;
    HDstrcpy(config->md_file_path, "./my_md_file");

    /* Enable VFD SWMR configuration */
    if(H5Pset_vfd_swmr_config(fapl, config) < 0)
        return -1;

    /* Open the file */
    if((fid = H5Fopen(filename, H5F_ACC_RDWR, fapl)) < 0)
        return -1;

    /* Close file access property list */
    if(H5Pclose(fapl) < 0)
        return -1;
    
    if(config)
        HDfree(config);

    /* Emit informational message */
    if(verbose)
        fprintf(stderr, "Opening datasets\n");

    /* Seed the random number generator with the attribute in the file */
    if((aid = H5Aopen(fid, "seed", H5P_DEFAULT)) < 0)
        return -1;
    if(H5Aread(aid, H5T_NATIVE_UINT, &seed) < 0)
        return -1;
    if(H5Aclose(aid) < 0)
        return -1;
    HDsrandom(seed);

    /* Open the datasets */
    for(u = 0; u < NLEVELS; u++)
        for(v = 0; v < symbol_count[u]; v++) {
            if((symbol_info[u][v].dsid = H5Dopen2(fid, symbol_info[u][v].name, H5P_DEFAULT)) < 0)
                return(-1);
            symbol_info[u][v].nrecords = 0;
        } /* end for */

    return fid;
} /* open_skeleton() */


/*-------------------------------------------------------------------------
 * Function:    add_records
 *
 * Purpose:     Writes a specified number of records to random datasets in
 *              the SWMR test file.
 *
 * Parameters:  hid_t fid
 *              The file ID of the SWMR HDF5 file
 *
 *              unsigned verbose
 *              Whether or not to emit verbose console messages
 *
 *              unsigned long nrecords
 *              # of records to write to the datasets
 *
 *              unsigned long flush_count
 *              # of records to write before flushing the file to disk
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
add_records(hid_t fid, unsigned verbose, unsigned long nrecords, unsigned long flush_count)
{
    hid_t tid;                          /* Datatype ID for records */
    hid_t mem_sid;                      /* Memory dataspace ID */
    hsize_t start[2] = {0, 0};          /* Hyperslab selection values */
    hsize_t count[2] = {1, 1};          /* Hyperslab selection values */
    symbol_t record;                    /* The record to add to the dataset */
    unsigned long rec_to_flush;         /* # of records left to write before flush */
#ifdef OUT
    volatile int dummy;                 /* Dummy varialbe for busy sleep */
#endif /* OUT */
    hsize_t dim[2] = {1,0};             /* Dataspace dimensions */
    unsigned long u, v;                 /* Local index variables */

    HDassert(fid >= 0);

    /* Reset the record */
    /* (record's 'info' field might need to change for each record written, also) */
    HDmemset(&record, 0, sizeof(record));

    /* Create a dataspace for the record to add */
    if((mem_sid = H5Screate(H5S_SCALAR)) < 0)
        return -1;

    /* Create datatype for appending records */
    if((tid = create_symbol_datatype()) < 0)
        return -1;

    /* Add records to random datasets, according to frequency distribution */
    rec_to_flush = flush_count;
    for(u = 0; u < nrecords; u++) {
        symbol_info_t *symbol;  /* Symbol to write record to */
        hid_t file_sid;         /* Dataset's space ID */
        hid_t aid;              /* Attribute ID */
        hbool_t corked;         /* Whether the dataset was corked */

        /* Get a random dataset, according to the symbol distribution */
        symbol = choose_dataset();

        /* If this is the first time the dataset has been opened, extend it and
         * add the sequence attribute */
        if(symbol->nrecords == 0) {
            symbol->nrecords = nrecords / 5;
            dim[1] = symbol->nrecords;

            /* Cork the metadata cache, to prevent the object header from being
             * flushed before the data has been written */
            if(H5Odisable_mdc_flushes(symbol->dsid) < 0)
                return -1;
            corked = TRUE;

            if(H5Dset_extent(symbol->dsid, dim) < 0)
                return -1;

            if((file_sid = H5Screate(H5S_SCALAR)) < 0)
                return -1;
            if((aid = H5Acreate2(symbol->dsid, "seq", H5T_NATIVE_ULONG, file_sid, H5P_DEFAULT, H5P_DEFAULT)) < 0)
                return -1;
            if(H5Sclose(file_sid) < 0)
                return -1;
        } /* end if */
        else {
            if((aid = H5Aopen(symbol->dsid, "seq", H5P_DEFAULT)) < 0)
                return -1;
            corked = FALSE;
        } /* end else */

        /* Get the coordinate to write */
        start[1] = (hsize_t)HDrandom() % symbol->nrecords;

        /* Set the record's ID (equal to its position) */
        record.rec_id = start[1];

        /* Get the dataset's dataspace */
        if((file_sid = H5Dget_space(symbol->dsid)) < 0)
            return -1;

        /* Choose a random record in the dataset */
        if(H5Sselect_hyperslab(file_sid, H5S_SELECT_SET, start, NULL, count, NULL) < 0)
            return -1;

        /* Write record to the dataset */
        if(H5Dwrite(symbol->dsid, tid, mem_sid, file_sid, H5P_DEFAULT, &record) < 0)
            return -1;

        /* Write the sequence number attribute.  Since we synchronize the random
         * number seed, the readers will always generate the same sequence of
         * randomly chosen datasets and offsets.  Therefore, and because of the
         * flush dependencies on the object header, the reader will be
         * guaranteed to see the written data if the sequence attribute is >=u.
         */
        if(H5Awrite(aid, H5T_NATIVE_ULONG, &u) < 0)
            return -1;

        /* Close the attribute */
        if(H5Aclose(aid) < 0)
            return -1;

        /* Uncork the metadata cache, if it's been */
        if(corked)
            if(H5Oenable_mdc_flushes(symbol->dsid) < 0)
                return -1;

        /* Close the dataset's dataspace */
        if(H5Sclose(file_sid) < 0)
            return -1;

        /* Check for flushing file */
        if(flush_count > 0) {
            /* Decrement count of records to write before flushing */
            rec_to_flush--;

            /* Check for counter being reached */
            if(0 == rec_to_flush) {
#ifdef TEMP_OUT
                /* Flush contents of file */
                if(H5Fflush(fid, H5F_SCOPE_GLOBAL) < 0)
                    return -1;
#endif

                /* Reset flush counter */
                rec_to_flush = flush_count;
            } /* end if */
        } /* end if */

#ifdef OUT
        /* Busy wait, to let readers catch up */
        /* If this is removed, also remove the BUSY_WAIT symbol
         * at the top of the file.
         */
        dummy = 0;
        for(v=0; v<BUSY_WAIT; v++)
            dummy++;
        if((unsigned long)dummy != v)
            return -1;
#endif /* OUT */

    } /* end for */

    /* Close the memory dataspace */
    if(H5Sclose(mem_sid) < 0)
        return -1;

    /* Close the datatype */
    if(H5Tclose(tid) < 0)
        return -1;

    /* Emit informational message */
    if(verbose)
        fprintf(stderr, "Closing datasets\n");

    /* Close the datasets */
    for(u = 0; u < NLEVELS; u++)
        for(v = 0; v < symbol_count[u]; v++)
            if(H5Dclose(symbol_info[u][v].dsid) < 0)
                return -1;

    return 0;
} /* add_records() */

static void
usage(void)
{
    printf("\n");
    printf("Usage error!\n");
    printf("\n");
    printf("Usage: vfd_swmr_sparse_writer [-q] [-f <# of records to write between\n");
    printf("    flushing file contents>] <# of records>\n");
    printf("\n");
    printf("<# of records to write between flushing file contents> should be 0\n");
    printf("(for no flushing) or between 1 and (<# of records> - 1)\n");
    printf("\n");
    printf("Defaults to verbose (no '-q' given) and flushing every 1000 records\n");
    printf("('-f 1000')\n");
    printf("\n");
    HDexit(1);
} /* usage() */

int main(int argc, const char *argv[])
{
    hid_t fid;                  /* File ID for file opened */
    long nrecords = 0;          /* # of records to append */
    long flush_count = 1000;    /* # of records to write between flushing file */
    unsigned verbose = 1;       /* Whether to emit some informational messages */
    unsigned u;                 /* Local index variable */

    /* Parse command line options */
    if(argc < 2)
        usage();
    if(argc > 1) {
        u = 1;
        while(u < (unsigned)argc) {
            if(argv[u][0] == '-') {
                switch(argv[u][1]) {
                    /* # of records to write between flushing file */
                    case 'f':
                        flush_count = HDatol(argv[u + 1]);
                        if(flush_count < 0)
                            usage();
                        u += 2;
                        break;

                    /* Be quiet */
                    case 'q':
                        verbose = 0;
                        u++;
                        break;

                    default:
                        usage();
                        break;
                } /* end switch */
            } /* end if */
            else {
                /* Get the number of records to append */
                nrecords = HDatol(argv[u]);
                if(nrecords <= 0)
                    usage();

                u++;
            } /* end else */
        } /* end while */
    } /* end if */
    if(nrecords <= 0)
        usage();
    if(flush_count >= nrecords)
        usage();

    /* Emit informational message */
    if(verbose) {
        HDfprintf(stderr, "Parameters:\n");
        HDfprintf(stderr, "\t# of records between flushes = %ld\n", flush_count);
        HDfprintf(stderr, "\t# of records to write = %ld\n", nrecords);
    } /* end if */

    /* Emit informational message */
    if(verbose)
        HDfprintf(stderr, "Generating symbol names\n");

    /* Generate dataset names */
    if(generate_symbols() < 0)
        return -1;

    /* Emit informational message */
    if(verbose)
        HDfprintf(stderr, "Opening skeleton file: %s\n", FILENAME);

    /* Open file skeleton */
    if((fid = open_skeleton(FILENAME, verbose)) < 0) {
        HDfprintf(stderr, "Error opening skeleton file!\n");
        HDexit(1);
    } /* end if */

    /* Send a message to indicate "H5Fopen" is complete--releasing the file lock */
    h5_send_message(WRITER_MESSAGE, NULL, NULL);

    /* Emit informational message */
    if(verbose)
        HDfprintf(stderr, "Adding records\n");

    /* Append records to datasets */
    if(add_records(fid, verbose, (unsigned long)nrecords, (unsigned long)flush_count) < 0) {
        HDfprintf(stderr, "Error appending records to datasets!\n");
        HDexit(1);
    } /* end if */

    /* Emit informational message */
    if(verbose)
        HDfprintf(stderr, "Releasing symbols\n");

    /* Clean up the symbols */
    if(shutdown_symbols() < 0) {
        HDfprintf(stderr, "Error releasing symbols!\n");
        HDexit(1);
    } /* end if */

    /* Emit informational message */
    if(verbose)
        HDfprintf(stderr, "Closing objects\n");

    /* Close objects opened */
    if(H5Fclose(fid) < 0) {
        HDfprintf(stderr, "Error closing file!\n");
        HDexit(1);
    } /* end if */

    return 0;
} /* main() */