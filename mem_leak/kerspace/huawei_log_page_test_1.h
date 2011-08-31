#ifndef __HUAWEI_LOG_PAGE_TEST_1
#define __HUAWEI_LOG_PAGE_TEST_1

void page_busy_test(struct page **pagep, unsigned int order);
void page_busy_test_exit(struct page **pagep, unsigned int order);

#endif
