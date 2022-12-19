/*
 * $HEADER$
 */
#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

extern const mca_base_component_t mca_bcol_basesmuma_component;
extern const mca_base_component_t mca_bcol_ptpcoll_component;

const mca_base_component_t *mca_bcol_base_static_components[] = {
  &mca_bcol_basesmuma_component, 
  &mca_bcol_ptpcoll_component, 
  NULL
};

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif

