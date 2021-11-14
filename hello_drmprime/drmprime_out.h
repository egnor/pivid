struct AVFrame;
typedef struct drmprime_out_env_s drmprime_out_env_t;

int drmprime_out_display(drmprime_out_env_t * dpo, struct AVFrame * frame);
void drmprime_out_delete(drmprime_out_env_t * dpo);
drmprime_out_env_t * drmprime_out_new();

