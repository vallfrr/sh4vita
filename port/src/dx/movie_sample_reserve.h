/* Transactionally grow reusable guest buffers; keep old blocks on failure. */
static int movie_samples_reserve(uint32_t *buffers, uint32_t *caps,
                                 unsigned count, uint32_t size) {
    uint32_t staged[NSAMP] = {0};
    if (!size || count > NSAMP) return 0;
    for (unsigned i = 0; i < count; ++i) {
        if (caps[i] >= size && buffers[i]) continue;
        staged[i] = gheap_calloc(size);
        if (!staged[i]) {
            for (unsigned j = 0; j < i; ++j) if (staged[j]) gheap_free(staged[j]);
            return 0;
        }
    }
    for (unsigned i = 0; i < count; ++i) if (staged[i]) {
        gheap_free(buffers[i]);
        buffers[i] = staged[i];
        caps[i] = size;
    }
    return 1;
}
