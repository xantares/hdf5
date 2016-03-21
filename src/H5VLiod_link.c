/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "H5VLiod_server.h"

#ifdef H5_HAVE_EFF

/*
 * Programmer:  Mohamad Chaarawi <chaarawi@hdfgroup.gov>
 *              June, 2013
 *
 * Purpose:	The IOD plugin server side link routines.
 */

static herr_t H5VL__iod_link_iterate(iod_handle_t coh, iod_obj_id_t obj_id, iod_handle_t obj_oh, 
    const char *path, uint32_t cs_scope, iod_trans_id_t rtid, hbool_t recursive, void *udata);


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_create_cb
 *
 * Purpose:	Creates a new link in the container (Hard or Soft).
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              May, 2013
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_create_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                               size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                               size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                               void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_create_in_t *input = (link_create_in_t *)op_data->input;
    H5VL_link_create_type_t create_type = input->create_type;
    iod_handle_t coh = input->coh; /* the container handle */
    iod_trans_id_t wtid = input->trans_num;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    iod_handles_t src_oh; /* The handle for creation src object */
    iod_obj_id_t src_id; /* The ID of the creation src object */
    iod_handles_t target_oh;
    iod_obj_id_t target_id; /* The ID of the target object where link is created*/
    char *src_last_comp = NULL, *dst_last_comp = NULL;
    iod_ret_t ret;
    herr_t ret_value = SUCCEED;

#if H5_EFF_DEBUG
    fprintf(stderr, "Start Link create\n");
#endif

    /* the traversal will retrieve the location where the link needs
       to be created from. The traversal will fail if an intermediate group
       does not exist. */
    ret = H5VL_iod_server_traverse(coh, input->loc_id, input->loc_oh, input->loc_name, 
                                   wtid, rtid, FALSE, cs_scope, &src_last_comp, &src_id, &src_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't traverse path");

#if H5_EFF_DEBUG
    fprintf(stderr, "new link name = %s\n", src_last_comp);
#endif

    if(H5VL_LINK_CREATE_HARD == create_type) {
        scratch_pad sp;
        iod_checksum_t sp_cs = 0;
        iod_handles_t mdkv_oh;
        uint64_t link_count = 0;
        hbool_t opened_locally = FALSE;

        if(input->target_loc_oh.rd_oh.cookie == IOD_OH_UNDEFINED) {
            /* Try and open the starting location */
            ret = iod_obj_open_read(coh, input->target_loc_id, wtid, NULL, 
                                    &input->target_loc_oh.rd_oh, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't open start location");
            opened_locally = TRUE;
        }
        /* Traverse Path and open the target object */
        ret = H5VL_iod_server_open_path(coh, input->target_loc_id, input->target_loc_oh, 
                                        input->target_name, rtid, cs_scope, &target_id, &target_oh);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't open object");

        /* add link in parent group to current object */
        ret = H5VL_iod_insert_new_link(src_oh.wr_oh, wtid, src_last_comp, 
                                       H5L_TYPE_HARD, &target_id, cs_scope, NULL, NULL);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't insert KV value");

        if(input->target_loc_id != target_id) {
            /* get scratch pad */
            ret = iod_obj_get_scratch(target_oh.rd_oh, rtid, &sp, &sp_cs, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't get scratch pad for object");

            if(sp_cs && (cs_scope & H5_CHECKSUM_IOD)) {
                /* verify scratch pad integrity */
                if(H5VL_iod_verify_scratch_pad(&sp, sp_cs) < 0)
                    HGOTO_ERROR_FF(FAIL, "Scratch Pad failed integrity check");
            }

            /* open the metadata KV */
            ret = iod_obj_open_read(coh, sp[0], rtid, NULL, &mdkv_oh.rd_oh, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't open scratch pad");
            ret = iod_obj_open_write(coh, sp[0], rtid, NULL, &mdkv_oh.wr_oh, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't open scratch pad");
        }
        else {
            /* open the metadata KV */
            ret = iod_obj_open_read(coh, input->target_mdkv_id, rtid, NULL, &mdkv_oh.rd_oh, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't open scratch pad");
            ret = iod_obj_open_write(coh, input->target_mdkv_id, rtid, NULL, &mdkv_oh.wr_oh, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't open scratch pad");
        }

        ret = H5VL_iod_get_metadata(mdkv_oh.rd_oh, rtid, H5VL_IOD_LINK_COUNT, 
                                    H5VL_IOD_KEY_OBJ_LINK_COUNT,
                                    cs_scope, NULL, &link_count);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "failed to retrieve link count");

        link_count ++;

        /* insert link count metadata */
        ret = H5VL_iod_insert_link_count(mdkv_oh.wr_oh, wtid, link_count, 
                                         cs_scope, NULL, NULL);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't insert KV value");

        /* close the metadata scratch pad */
        ret = iod_obj_close(mdkv_oh.rd_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
        ret = iod_obj_close(mdkv_oh.wr_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");

        /* close the target location */
        if(TRUE == opened_locally || 
           input->target_loc_oh.rd_oh.cookie != target_oh.rd_oh.cookie) {
            ret = iod_obj_close(target_oh.rd_oh, NULL, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "can't close object");
        }
    }
    else if(H5VL_LINK_CREATE_SOFT == create_type) {
        /* add link in parent group to the source location */
        ret = H5VL_iod_insert_new_link(src_oh.wr_oh, wtid, src_last_comp, 
                                       H5L_TYPE_SOFT, input->link_value, 
                                       cs_scope, NULL, NULL);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't insert KV value");

#if H5_EFF_DEBUG
        fprintf(stderr, "Soft link Value = %s\n", input->link_value);
#endif
    }
    else
        HGOTO_ERROR_FF(FAIL, "Invalid Link type");

    /* close the source location */
    if(input->loc_oh.rd_oh.cookie != src_oh.rd_oh.cookie) {
        ret = iod_obj_close(src_oh.rd_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }
    if(input->loc_oh.wr_oh.cookie != src_oh.wr_oh.cookie) {
        ret = iod_obj_close(src_oh.wr_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }

done:
#if H5_EFF_DEBUG
    fprintf(stderr, "Done with link create, sending response %d to client\n", 
            ret_value);
#endif

    HG_Handler_start_output(op_data->hg_handle, &ret_value);

    src_last_comp = (char *)H5MM_xfree(src_last_comp);
    dst_last_comp = (char *)H5MM_xfree(dst_last_comp);

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);
    input = (link_create_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

} /* end H5VL_iod_server_link_create_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_move_cb
 *
 * Purpose:	Moves/Copies a link in the container.
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              May, 2013
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_move_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                             size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                             size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                             void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_move_in_t *input = (link_move_in_t *)op_data->input;
    hbool_t copy_flag = input->copy_flag;
    iod_handle_t coh = input->coh; /* the container handle */
    iod_trans_id_t wtid = input->trans_num;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    iod_handles_t src_oh; /* The handle for src object group */
    iod_obj_id_t src_id; /* The ID of the src object */
    iod_handles_t dst_oh; /* The handle for the dst object where link is created*/
    iod_obj_id_t dst_id; /* The ID of the dst object where link is created*/
    char *src_last_comp = NULL, *dst_last_comp = NULL;
    iod_kv_t kv;
    iod_ret_t ret;
    H5VL_iod_link_t iod_link;
    herr_t ret_value = SUCCEED;

#if H5_EFF_DEBUG
    fprintf(stderr, "Start link move SRC %s DST %s (%"PRIu64", %"PRIu64") to (%"PRIu64", %"PRIu64")\n",
            input->src_loc_name, input->dst_loc_name, 
            input->src_loc_oh.wr_oh.cookie, input->src_loc_oh.rd_oh.cookie,
            input->dst_loc_oh.wr_oh.cookie, input->dst_loc_oh.rd_oh.cookie);
#endif

    /* the traversal will retrieve the location where the link needs
       to be moved/copied from. The traversal will fail if an intermediate group
       does not exist. */
    ret = H5VL_iod_server_traverse(coh, input->src_loc_id, input->src_loc_oh, 
                                   input->src_loc_name, wtid, rtid, FALSE, cs_scope, 
                                   &src_last_comp, &src_id, &src_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't traverse path");

    /* the traversal will retrieve the location where the link needs
       to be moved/copied to. The traversal will fail if an intermediate group
       does not exist. */
    ret = H5VL_iod_server_traverse(coh, input->dst_loc_id, input->dst_loc_oh, 
                                   input->dst_loc_name, wtid, rtid, FALSE, cs_scope, 
                                   &dst_last_comp, &dst_id, &dst_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't traverse path");

    /* get the link value */
    ret = H5VL_iod_get_metadata(src_oh.rd_oh, rtid, H5VL_IOD_LINK, 
                                src_last_comp, cs_scope, NULL, &iod_link);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "failed to retrieve link value");

    /* Insert object in the destination path */
    if(H5L_TYPE_HARD == iod_link.link_type) {
        ret = H5VL_iod_insert_new_link(dst_oh.wr_oh, wtid, dst_last_comp, 
                                       iod_link.link_type, &iod_link.u.iod_id, 
                                       cs_scope, NULL, NULL);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't insert KV value");
    }
    else if(H5L_TYPE_SOFT == iod_link.link_type) {
        ret = H5VL_iod_insert_new_link(dst_oh.wr_oh, wtid, dst_last_comp, 
                                       iod_link.link_type, &iod_link.u.symbolic_name, 
                                       cs_scope, NULL, NULL);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't insert KV value");
    }

    /* if the operation type is a Move, remove the KV pair from the source object */
    if(!copy_flag) {
        iod_kv_params_t kvs;
        iod_ret_t ret;
        iod_checksum_t cs;

        kv.key = src_last_comp;
        kv.key_len = strlen(src_last_comp) + 1;
        kvs.kv = &kv;
        kvs.cs = &cs;
        kvs.ret = &ret;

        /* remove link from source object */
        ret = iod_kv_unlink_keys(src_oh.wr_oh, wtid, NULL, 1, &kvs, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "Unable to unlink KV pair");
    }

    /* adjust link count on target object */
    {
        iod_handle_t target_oh;
        iod_handles_t mdkv_oh;
        scratch_pad sp;
        iod_checksum_t sp_cs = 0;
        uint64_t link_count = 0;

        /* open the current group */
        ret = iod_obj_open_read(coh, iod_link.u.iod_id, rtid, NULL, &target_oh, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't open current group");

        /* get scratch pad */
        ret = iod_obj_get_scratch(target_oh, rtid, &sp, &sp_cs, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't get scratch pad for object");
        if(sp_cs && (cs_scope & H5_CHECKSUM_IOD)) {
            /* verify scratch pad integrity */
            if(H5VL_iod_verify_scratch_pad(&sp, sp_cs) < 0)
                HGOTO_ERROR_FF(FAIL, "Scratch Pad failed integrity check");
        }

        /* open the metadata scratch pad */
        ret = iod_obj_open_read(coh, sp[0], rtid, NULL, &mdkv_oh.rd_oh, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't open scratch pad");
        ret = iod_obj_open_write(coh, sp[0], rtid, NULL, &mdkv_oh.wr_oh, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't open scratch pad");

        ret = H5VL_iod_get_metadata(mdkv_oh.rd_oh, rtid, H5VL_IOD_LINK_COUNT, 
                                    H5VL_IOD_KEY_OBJ_LINK_COUNT,
                                    cs_scope, NULL, &link_count);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "failed to retrieve link count");

        link_count ++;

        /* insert link count metadata */
        ret = H5VL_iod_insert_link_count(mdkv_oh.wr_oh, wtid, link_count, 
                                         cs_scope, NULL, NULL);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "can't insert KV value");

        /* close the metadata scratch pad */
        ret = iod_obj_close(mdkv_oh.rd_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
        ret = iod_obj_close(mdkv_oh.wr_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");

        /* close the target location */
        ret = iod_obj_close(target_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }

    /* close source group if it is not the location we started the
       traversal into */
    if(input->src_loc_oh.rd_oh.cookie != src_oh.rd_oh.cookie) {
        ret = iod_obj_close(src_oh.rd_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }
    if(input->src_loc_oh.wr_oh.cookie != src_oh.wr_oh.cookie) {
        ret = iod_obj_close(src_oh.wr_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }

    /* close parent group if it is not the location we started the
       traversal into */
    if(input->dst_loc_oh.rd_oh.cookie != dst_oh.rd_oh.cookie) {
        ret = iod_obj_close(dst_oh.rd_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }
    if(input->dst_loc_oh.wr_oh.cookie != dst_oh.wr_oh.cookie) {
        ret = iod_obj_close(dst_oh.wr_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close object");
    }

done:
#if H5_EFF_DEBUG
    fprintf(stderr, "Done with link move, sending response %d to client\n",
            ret_value);
#endif

    HG_Handler_start_output(op_data->hg_handle, &ret_value);

    src_last_comp = (char *)H5MM_xfree(src_last_comp);
    dst_last_comp = (char *)H5MM_xfree(dst_last_comp);

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);
    input = (link_move_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

    if(iod_link.link_type == H5L_TYPE_SOFT) {
        if(iod_link.u.symbolic_name)
            free(iod_link.u.symbolic_name);
    }

} /* end H5VL_iod_server_link_move_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_exists_cb
 *
 * Purpose:	Checks if a link exists.
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              May, 2013
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_exists_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                               size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                               size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                               void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_op_in_t *input = (link_op_in_t *)op_data->input;
    iod_handle_t coh = input->coh;
    iod_handles_t loc_oh = input->loc_oh;
    iod_obj_id_t loc_id = input->loc_id;
    iod_handles_t cur_oh;
    iod_obj_id_t cur_id;
    const char *loc_name = input->path;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    char *last_comp = NULL;
    htri_t ret = -1;
    iod_ret_t iod_ret;
    iod_size_t val_size = 0;
    herr_t ret_value = SUCCEED;

#if H5_EFF_DEBUG
    fprintf(stderr, "Start link Exists for %s on CV %d\n", loc_name, (int)rtid);
#endif

    /* the traversal will retrieve the location where the link needs
       to be checked */
    if(H5VL_iod_server_traverse(coh, loc_id, loc_oh, loc_name, rtid, rtid, FALSE, 
                                cs_scope, &last_comp, &cur_id, &cur_oh) < 0) {
        ret = FALSE;
        HGOTO_DONE(SUCCEED);
    }

    /* check the last component */
    if(iod_kv_get_value(cur_oh.rd_oh, rtid, last_comp, (iod_size_t)strlen(last_comp) + 1,
                        NULL, &val_size, NULL, NULL) < 0) {
        ret = FALSE;
    } /* end if */
    else {
        H5VL_iod_link_t iod_link;

        if(H5VL_iod_get_metadata(cur_oh.rd_oh, rtid, H5VL_IOD_LINK, 
                                 last_comp, cs_scope, NULL, &iod_link) < 0) {
            ret = FALSE;
        }
        else {
            iod_handle_t rd_oh;

            if (iod_obj_open_read(coh, iod_link.u.iod_id, rtid, NULL, &rd_oh, NULL) < 0) {
                ret = FALSE;
            }
            else {
                iod_ret = iod_obj_close(rd_oh, NULL, NULL);
                if(iod_ret < 0)
                    HGOTO_ERROR_FF(iod_ret, "can't close current object handle");
                ret = TRUE;
            }
        }
    }

done:

    /* close parent group if it is not the location we started the
       traversal into */
    if(input->loc_oh.rd_oh.cookie != cur_oh.rd_oh.cookie) {
        iod_obj_close(cur_oh.rd_oh, NULL, NULL);
    }
    if(input->loc_oh.wr_oh.cookie != cur_oh.wr_oh.cookie) {
        iod_obj_close(cur_oh.wr_oh, NULL, NULL);
    }

#if H5_EFF_DEBUG
    fprintf(stderr, "Done with link exists, sending %d to client\n", ret);
#endif

    HG_Handler_start_output(op_data->hg_handle, &ret);

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);
    input = (link_op_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

    last_comp = (char *)H5MM_xfree(last_comp);

} /* end H5VL_iod_server_link_exists_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_get_info_cb
 *
 * Purpose:	Checks if a link get_info.
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              May, 2013
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_get_info_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                                 size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                                 size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                                 void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_op_in_t *input = (link_op_in_t *)op_data->input;
    H5L_ff_info_t linfo;
    iod_handle_t coh = input->coh;
    iod_handles_t loc_oh = input->loc_oh;
    iod_obj_id_t loc_id = input->loc_id;
    iod_handles_t cur_oh;
    iod_obj_id_t cur_id;
    const char *loc_name = input->path;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    char *last_comp = NULL;
    H5VL_iod_link_t iod_link;
    iod_ret_t ret;
    herr_t ret_value = SUCCEED;

    /* the traversal will retrieve the location where the link needs
       to be checked */
    ret = H5VL_iod_server_traverse(coh, loc_id, loc_oh, loc_name, rtid, rtid, FALSE, 
                                   cs_scope, &last_comp, &cur_id, &cur_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't traverse path");

#if H5_EFF_DEBUG
    fprintf(stderr, "Link Get_Info on link %s\n", last_comp);
#endif
    
    /* lookup link information in the current location */
    ret = H5VL_iod_get_metadata(cur_oh.rd_oh, rtid, H5VL_IOD_LINK, 
                                last_comp, cs_scope, NULL, &iod_link);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "failed to retrieve link value");

    /* setup link info */
    linfo.type = iod_link.link_type;
    linfo.cset = 0;
    switch (linfo.type) {
    case H5L_TYPE_HARD:
        linfo.u.address = iod_link.u.iod_id;
        break;
    case H5L_TYPE_SOFT:
        linfo.u.val_size = strlen(iod_link.u.symbolic_name) + 1;
        break;
    case H5L_TYPE_ERROR:
    case H5L_TYPE_EXTERNAL:
    case H5L_TYPE_MAX:
    default:
        HGOTO_ERROR_FF(FAIL, "unsuppored link type");
    }

#if H5_EFF_DEBUG
    fprintf(stderr, "Done with link get_info, sending response to client\n");
#endif

    HG_Handler_start_output(op_data->hg_handle, &linfo);

done:

    if(ret_value < 0) {
        fprintf(stderr, "FAILED link get_info, sending ERROR to client\n");
        linfo.type = H5L_TYPE_ERROR;
        HG_Handler_start_output(op_data->hg_handle, &linfo);
    }

    /* close parent group if it is not the location we started the
       traversal into */
    if(input->loc_oh.rd_oh.cookie != cur_oh.rd_oh.cookie) {
        iod_obj_close(cur_oh.rd_oh, NULL, NULL);
    }
    if(input->loc_oh.wr_oh.cookie != cur_oh.wr_oh.cookie) {
        iod_obj_close(cur_oh.wr_oh, NULL, NULL);
    }

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);
    input = (link_op_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

    last_comp = (char *)H5MM_xfree(last_comp);

    if(iod_link.link_type == H5L_TYPE_SOFT) {
        if(iod_link.u.symbolic_name)
            free(iod_link.u.symbolic_name);
    }

} /* end H5VL_iod_server_link_get_info_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_get_val_cb
 *
 * Purpose:	Get comment for an object.
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              May, 2013
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_get_val_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                                size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                                size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                                void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_get_val_in_t *input = (link_get_val_in_t *)op_data->input;
    link_get_val_out_t output;
    iod_handle_t coh = input->coh;
    iod_handles_t loc_oh = input->loc_oh;
    iod_obj_id_t loc_id = input->loc_id;
    size_t length = input->length;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    iod_handles_t cur_oh;
    iod_obj_id_t cur_id;
    const char *loc_name = input->path;
    char *last_comp = NULL;
    H5VL_iod_link_t iod_link;
    iod_ret_t ret;
    herr_t ret_value = SUCCEED;

    /* the traversal will retrieve the location where the link needs
       to be checked */
    ret = H5VL_iod_server_traverse(coh, loc_id, loc_oh, loc_name, rtid, rtid, FALSE, 
                                   cs_scope, &last_comp, &cur_id, &cur_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't traverse path");

#if H5_EFF_DEBUG
    fprintf(stderr, "Link Get_val on link %s\n", last_comp);
#endif
    
    /* lookup link information in the current location */
    ret = H5VL_iod_get_metadata(cur_oh.rd_oh, rtid, H5VL_IOD_LINK, 
                                last_comp, cs_scope, NULL, &iod_link);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "failed to retrieve link value");

    if(H5L_TYPE_SOFT != iod_link.link_type)
        HGOTO_ERROR_FF(FAIL, "link is not SOFT");

    output.value.val_size = length;
    output.value.val = NULL;

    if(length) {
        if(NULL == (output.value.val = (void *)malloc (length)))
            HGOTO_ERROR_FF(FAIL, "can't allocate value buffer");
        memcpy(output.value.val, iod_link.u.symbolic_name, length);
    }

    output.ret = ret_value;
    
    HG_Handler_start_output(op_data->hg_handle, &output);

done:
#if H5_EFF_DEBUG
    fprintf(stderr, "Done with get link_val, sending (%s) response to client\n", 
            (char *)output.value.val);
#endif
    if(ret_value < 0) {
        output.ret = ret_value;
        output.value.val = NULL;
        output.value.val_size = 0;
        HG_Handler_start_output(op_data->hg_handle, &output);
    }

    /* close parent group if it is not the location we started the
       traversal into */
    if(input->loc_oh.rd_oh.cookie != cur_oh.rd_oh.cookie) {
        iod_obj_close(cur_oh.rd_oh, NULL, NULL);
    }
    if(input->loc_oh.wr_oh.cookie != cur_oh.wr_oh.cookie) {
        iod_obj_close(cur_oh.wr_oh, NULL, NULL);
    }

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);
    input = (link_get_val_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

    last_comp = (char *)H5MM_xfree(last_comp);

    if(output.value.val)
        free(output.value.val);

    if(iod_link.link_type == H5L_TYPE_SOFT) {
        if(iod_link.u.symbolic_name)
            free(iod_link.u.symbolic_name);
    }

} /* end H5VL_iod_server_link_get_val_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_remove_cb
 *
 * Purpose:	Removes a link from a container.
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              May, 2013
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_remove_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                               size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                               size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                               void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_op_in_t *input = (link_op_in_t *)op_data->input;
    iod_handle_t coh = input->coh;
    iod_handles_t loc_oh = input->loc_oh;
    iod_obj_id_t loc_id = input->loc_id;
    iod_trans_id_t wtid = input->trans_num;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    iod_handles_t cur_oh;
    iod_obj_id_t cur_id;
    iod_handle_t obj_oh;
    iod_handles_t mdkv_oh;
    const char *loc_name = input->path;
    char *last_comp = NULL;
    iod_kv_params_t kvs;
    iod_kv_t kv;
    iod_ret_t ret;
    iod_checksum_t cs;
    H5VL_iod_link_t iod_link;
    int step = 0;
    herr_t ret_value = SUCCEED;

#if H5_EFF_DEBUG
    fprintf(stderr, "Start link Remove %s at (%"PRIu64", %"PRIu64")\n",
            loc_name, loc_oh.wr_oh.cookie, loc_oh.rd_oh.cookie);
#endif

    /* the traversal will retrieve the location where the link needs
       to be removed. The traversal will fail if an intermediate group
       does not exist. */
    ret = H5VL_iod_server_traverse(coh, loc_id, loc_oh, loc_name, wtid, rtid, 
                                   FALSE, cs_scope, &last_comp, &cur_id, &cur_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't traverse path");

    /* lookup object ID in the current location */
    ret = H5VL_iod_get_metadata(cur_oh.rd_oh, rtid, H5VL_IOD_LINK, 
                                last_comp, cs_scope, NULL, &iod_link);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "failed to retrieve link value");

    /* unlink object from conainer */
    kv.key = last_comp;
    kv.key_len = strlen(last_comp) + 1;
    kvs.kv = &kv;
    kvs.cs = &cs;
    kvs.ret = &ret;
    ret = iod_kv_unlink_keys(cur_oh.wr_oh, wtid, NULL, 1, &kvs, NULL);
    if(ret < 0)
        HGOTO_ERROR_FF(ret, "Unable to unlink KV pair");

    /* check the metadata information for the object and remove
       it from the container if this is the last link to it */
    if(iod_link.link_type == H5L_TYPE_HARD) {
        scratch_pad sp;
        iod_checksum_t sp_cs = 0;
        uint64_t link_count = 0;
        iod_obj_id_t obj_id;

        obj_id = iod_link.u.iod_id;

        /* open the current group */
        ret = iod_obj_open_read(coh, obj_id, rtid, NULL, &obj_oh, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't open current group");

        step ++;

        /* get scratch pad */
        ret = iod_obj_get_scratch(obj_oh, rtid, &sp, &sp_cs, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't get scratch pad for object");

        if(sp_cs && (cs_scope & H5_CHECKSUM_IOD)) {
            /* verify scratch pad integrity */
            if(H5VL_iod_verify_scratch_pad(&sp, sp_cs) < 0)
                HGOTO_ERROR_FF(FAIL, "Scratch Pad failed integrity check");
        }

        /* open the metadata scratch pad */
        ret = iod_obj_open_read(coh, sp[0], rtid, NULL, &mdkv_oh.rd_oh, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't open scratch pad");
        ret = iod_obj_open_write(coh, sp[0], rtid, NULL, &mdkv_oh.wr_oh, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't open scratch pad");

        step ++;

        ret = H5VL_iod_get_metadata(mdkv_oh.rd_oh, rtid, H5VL_IOD_LINK_COUNT, 
                                    H5VL_IOD_KEY_OBJ_LINK_COUNT,
                                    cs_scope, NULL, &link_count);
        if(ret != SUCCEED)
            HGOTO_ERROR_FF(ret, "failed to retrieve link count");

        link_count --;

        /* if this is not the only link to the object, update the link count */
        if(0 != link_count) {
            /* insert link count metadata */
            ret = H5VL_iod_insert_link_count(mdkv_oh.wr_oh, wtid, link_count, 
                                             cs_scope, NULL, NULL);
            if(ret != SUCCEED)
                HGOTO_ERROR_FF(ret, "can't insert KV value");
        }

        ret = iod_obj_close(mdkv_oh.rd_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close IOD object");
        ret = iod_obj_close(mdkv_oh.wr_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close IOD object");

        step --;

        ret = iod_obj_close(obj_oh, NULL, NULL);
        if(ret < 0)
            HGOTO_ERROR_FF(ret, "can't close IOD object");

        step --;

        /* If this was the only link to the object, remove the object */
        if(0 == link_count) {
            ret = iod_obj_unlink(coh, obj_id, wtid, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "Unable to unlink object");
            ret = iod_obj_unlink(coh, sp[0], wtid, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "Unable to unlink MDKV object");
            ret = iod_obj_unlink(coh, sp[1], wtid, NULL);
            if(ret < 0)
                HGOTO_ERROR_FF(ret, "Unable to unlink ATTRKV object");
        }
    }

done:

    /* close parent group if it is not the location we started the
       traversal into */
    if(input->loc_oh.rd_oh.cookie != cur_oh.rd_oh.cookie) {
        iod_obj_close(cur_oh.rd_oh, NULL, NULL);
    }
    if(input->loc_oh.wr_oh.cookie != cur_oh.wr_oh.cookie) {
        iod_obj_close(cur_oh.wr_oh, NULL, NULL);
    }

    if(step == 2) {
        /* close the metadata scratch pad */
        iod_obj_close(mdkv_oh.rd_oh, NULL, NULL);
        iod_obj_close(mdkv_oh.wr_oh, NULL, NULL);
        step --;
    }
    if(step == 1) {
        iod_obj_close(obj_oh, NULL, NULL);
        step --;
    }

#if H5_EFF_DEBUG
    fprintf(stderr, "Done with link remove, sending response %d to client\n",
            ret_value);
#endif

    HG_Handler_start_output(op_data->hg_handle, &ret_value);

    last_comp = (char *)H5MM_xfree(last_comp);

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);
    input = (link_op_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

    if(iod_link.link_type == H5L_TYPE_SOFT) {
        if(iod_link.u.symbolic_name)
            free(iod_link.u.symbolic_name);
    }
        
} /* end H5VL_iod_server_link_remove_cb() */


/*-------------------------------------------------------------------------
 * Function:	H5VL_iod_server_link_iterate_cb
 *
 * Purpose:     iterates through all the objects under a group and 
 *              gathers their paths and info.
 *
 * Return:	Success:	SUCCEED 
 *		Failure:	Negative
 *
 * Programmer:  Mohamad Chaarawi
 *              December, 2015
 *
 *-------------------------------------------------------------------------
 */
void
H5VL_iod_server_link_iterate_cb(AXE_engine_t H5_ATTR_UNUSED axe_engine, 
                                 size_t H5_ATTR_UNUSED num_n_parents, AXE_task_t H5_ATTR_UNUSED n_parents[], 
                                 size_t H5_ATTR_UNUSED num_s_parents, AXE_task_t H5_ATTR_UNUSED s_parents[], 
                                 void *_op_data)
{
    op_data_t *op_data = (op_data_t *)_op_data;
    link_op_in_t *input = (link_op_in_t *)op_data->input;
    link_iterate_t output;
    iod_handle_t coh = input->coh;
    iod_handles_t loc_oh = input->loc_oh;
    iod_obj_id_t loc_id = input->loc_id;
    iod_trans_id_t rtid = input->rcxt_num;
    uint32_t cs_scope = input->cs_scope;
    iod_handles_t obj_oh;
    iod_obj_id_t obj_id;
    const char *loc_name = input->path;
    htri_t ret = -1;
    herr_t ret_value = SUCCEED;

#if H5_EFF_DEBUG
    fprintf(stderr, "Start link iterate on %s (OH %"PRIu64" ID %"PRIx64")\n", 
            loc_name, input->loc_oh.rd_oh.cookie, input->loc_id);
#endif

    /* Traverse Path and open object */
    ret = H5VL_iod_server_open_path(coh, loc_id, loc_oh, loc_name, rtid, 
                                    cs_scope, &obj_id, &obj_oh);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "can't open object");

    output.num_objs = 0;
    output.paths = NULL;
    output.linfos = NULL;

    ret = H5VL__iod_link_iterate(coh, obj_id, obj_oh.rd_oh, ".", cs_scope, rtid, input->recursive, &output);
    if(ret != SUCCEED)
        HGOTO_ERROR_FF(ret, "iterate objects failed");

    if(loc_oh.rd_oh.cookie != obj_oh.rd_oh.cookie && 
       iod_obj_close(obj_oh.rd_oh, NULL, NULL) < 0)
        HGOTO_ERROR_FF(FAIL, "can't close object");

done:
    output.ret = ret_value;

#if H5_EFF_DEBUG
    fprintf(stderr, "Done with Link Iterate, sending response to client\n");
#endif

    HG_Handler_start_output(op_data->hg_handle, &output);

    HG_Handler_free_input(op_data->hg_handle, input);
    HG_Handler_free(op_data->hg_handle);

    free(output.paths);
    free(output.linfos);

    input = (link_op_in_t *)H5MM_xfree(input);
    op_data = (op_data_t *)H5MM_xfree(op_data);

} /* end H5VL_iod_server_link_iterate_cb() */

static herr_t 
H5VL__iod_link_iterate(iod_handle_t coh, iod_obj_id_t obj_id, iod_handle_t obj_oh, const char *path,
                      uint32_t cs_scope, iod_trans_id_t rtid, hbool_t recursive, void *udata)
{
    link_iterate_t *out = (link_iterate_t *)udata;
    H5I_type_t obj_type;
    iod_ret_t ret = 0;
    herr_t ret_value = SUCCEED;

    /* Get the object type. */
    if((obj_type = H5VL__iod_get_h5_obj_type(obj_id, coh, rtid, cs_scope)) < 0)
        HGOTO_ERROR_FF(FAIL, "can't get object type");

    /* if this is a group, iterate through links in that group */
    if(H5I_GROUP == obj_type || H5I_FILE == obj_type) {
        int num_entries = 0;
        int cur_idx, u = 0;

        ret = iod_kv_get_num(obj_oh, rtid, &num_entries, NULL);
        if(ret != 0)
            HGOTO_ERROR_FF(FAIL, "can't get number of KV entries");

        if(0 != num_entries) {
            int i;
            iod_kv_params_t *kvs = NULL;
            iod_kv_t *kv = NULL;
            iod_checksum_t *oid_cs = NULL;
            iod_ret_t *oid_ret = NULL;

            /* add the current object to the output */
            cur_idx = out->num_objs;
            out->num_objs = out->num_objs + num_entries;
            out->paths = (const char **)realloc(out->paths, sizeof(char *) * out->num_objs);
            out->linfos = (H5L_ff_info_t *)realloc(out->linfos, sizeof(H5L_ff_info_t) * out->num_objs);

            kvs = (iod_kv_params_t *)malloc(sizeof(iod_kv_params_t) * (size_t)num_entries);
            kv = (iod_kv_t *)malloc(sizeof(iod_kv_t) * (size_t)num_entries);
            oid_cs = (iod_checksum_t *)malloc(sizeof(iod_checksum_t) * (size_t)num_entries);
            oid_ret = (iod_ret_t *)malloc(sizeof(iod_ret_t) * (size_t)num_entries);

            for(i=0 ; i<num_entries ; i++) {
                kv[i].key = malloc(IOD_KV_KEY_MAXLEN);
                kv[i].key_len = IOD_KV_KEY_MAXLEN;
                kvs[i].kv = &kv[i];
                kvs[i].cs = &oid_cs[i];
                kvs[i].ret = &oid_ret[i];
            }

            ret = iod_kv_list_key(obj_oh, rtid, NULL, 0, &num_entries, kvs, NULL);
            if(ret != 0)
                HGOTO_ERROR_FF(FAIL, "can't get list of keys");

            u=cur_idx;

            for(i=0 ; i<num_entries ; i++) {
                H5VL_iod_link_t value;
                iod_handle_t oh;
                char cur_path[IOD_KV_KEY_MAXLEN];

                /* lookup object in the current group */
                ret = H5VL_iod_get_metadata(obj_oh, rtid, H5VL_IOD_LINK, 
                                            (char *)(kv[i].key), cs_scope, NULL, &value);
                if(SUCCEED != ret)
                    HGOTO_ERROR_FF(ret, "failed to retrieve link value");

                if(strcmp(path, ".")) {
                    sprintf(cur_path, "%s/%s", path, ((char *)kv[i].key));
                    out->paths[u] = strdup(cur_path);
                }
                else
                    out->paths[u] = strdup((char *)(kv[i].key));

                /* setup link info */
                out->linfos[u].type = value.link_type;
                out->linfos[u].cset = 0;
                switch (out->linfos[u].type) {
                case H5L_TYPE_HARD:
                    out->linfos[u].u.address = value.u.iod_id;
                    break;
                case H5L_TYPE_SOFT:
                    out->linfos[u].u.val_size = strlen(value.u.symbolic_name) + 1;
                    break;
                case H5L_TYPE_ERROR:
                case H5L_TYPE_EXTERNAL:
                case H5L_TYPE_MAX:
                default:
                    HGOTO_ERROR_FF(FAIL, "unsuppored link type");
                }

                if(recursive && H5L_TYPE_HARD == out->linfos[u].type) {
                    ret = iod_obj_open_read(coh, value.u.iod_id, rtid, NULL, &oh, NULL);
                    if(ret < 0)
                        HGOTO_ERROR_FF(ret, "can't open object for read");

                    if(strcmp(path, "."))
                        sprintf(cur_path, "%s/%s", path, ((char *)kv[i].key));
                    else
                        sprintf(cur_path, "%s", ((char *)kv[i].key));

                    ret = H5VL__iod_link_iterate(coh, value.u.iod_id, oh, cur_path, cs_scope, 
                                                 rtid, recursive, udata);
                    if(ret != SUCCEED)
                        HGOTO_ERROR_FF(ret, "visit objects failed");

                    if(iod_obj_close(oh, NULL, NULL) < 0)
                        HGOTO_ERROR_FF(FAIL, "can't close object");
                }
                u++;
            }

            for(i=0 ; i<num_entries ; i++) {
                free(kv[i].key);
            }

            free(kv);
            free(oid_cs);
            free(oid_ret);
            free(kvs);
        }
    }

    ret_value = ret;

done:
    return ret_value;
} /* H5VL__iod_link_iterate */

#endif /* H5_HAVE_EFF */
