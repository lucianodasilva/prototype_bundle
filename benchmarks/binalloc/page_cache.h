#pragma once
#ifndef PROTOTYPE_BUNDLE_PAGE_CACHE_H
#define PROTOTYPE_BUNDLE_PAGE_CACHE_H

#include<cinttypes>

namespace sgc2 {

    struct page_meta;

    page_meta *page_cache_fetch(uint8_t bin_index);

    void page_cache_return(page_meta *pmeta);

    void page_cache_release(page_meta *pmeta);

}

#endif //PROTOTYPE_BUNDLE_PAGE_CACHE_H
