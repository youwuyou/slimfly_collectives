/*
 * $HEADER$
 */
#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

extern const mca_base_component_t mca_sbgp_basesmuma_component;
extern const mca_base_component_t mca_sbgp_basesmsocket_component;
extern const mca_base_component_t mca_sbgp_p2p_component;

const mca_base_component_t *mca_sbgp_base_static_components[] = {
  &mca_sbgp_basesmuma_component, 
  &mca_sbgp_basesmsocket_component, 
  &mca_sbgp_p2p_component, 
  NULL
};

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif

