#pragma once
#ifndef PROTOTYPE_BUNDLE_CLUSTER_CACHE_H
#define PROTOTYPE_BUNDLE_CLUSTER_CACHE_H

namespace sgc2 {

    struct cluster_meta;

    cluster_meta *cluster_cache_fetch();

    void cluster_cache_return(cluster_meta *cmeta);

    void cluster_cache_release(cluster_meta *cmeta);
}

#endif //PROTOTYPE_BUNDLE_CLUSTER_CACHE_H
