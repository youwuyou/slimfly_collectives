/*
 * $HEADER$
 */
#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

extern const mca_base_component_t mca_db_hash_component;
extern const mca_base_component_t mca_db_print_component;

const mca_base_component_t *mca_db_base_static_components[] = {
  &mca_db_hash_component, 
  &mca_db_print_component, 
  NULL
};

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif

