#ifndef __MEM_OVERFLOW_CHECK_H
#define __MEM_OVERFLOW_CHECK_H

void *skmalloc(size_t,gfp_t);
void skfree(const void *);
void *svmalloc(unsigned long);
void svfree(const void *);
unsigned long __sget_free_pages(gfp_t,unsigned int);
unsigned long __nget_free_pages(gfp_t,unsigned int);
void nfree_pages(unsigned long,unsigned int);
void sfree_pages(unsigned long,unsigned int);
void *__get_free_pages_ex(gfp_t,size_t);
void free_pages_ex(void *,size_t);
#define __sget_free_page(gfp_mask) \
	__sget_free_pages(gfp_mask,0)
#define __sget_dma_pages(gfp_mask,order)\
	__sget_free_pages((gfp_mask) | __GFP_DMA,(order))
#define sfree_page(addr) \
	sfree_pages((addr),0)


#endif
