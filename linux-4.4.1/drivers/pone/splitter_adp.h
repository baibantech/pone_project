/*************************************************************************
	> File Name: splitter_adp.h
	> Author: ma6174
	> Mail: ma6174@163.com 
	> Created Time: Fri 24 Mar 2017 01:01:03 AM PDT
 ************************************************************************/
#ifndef __SPLITTER_ADP_H__
#define __SPLITTER_ADP_H__
char *tree_get_key_from_data(char *pdata);
void tree_free_key(char *pkey);
void tree_free_data(char *pdata);
char *tree_construct_data_from_key(char *pkey);
int pone_case_init(void);
char *insert_sd_tree(unsigned long slice_idx);
int delete_sd_tree(unsigned long slice_idx,int op);
#endif
