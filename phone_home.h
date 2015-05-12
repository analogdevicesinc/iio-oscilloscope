#ifndef __PHONE_HOME_H__
#define __PHONE_HOME_H__

typedef struct _release Release;

struct _release {
	char *name;
	char *build_date;
	char *commit;
	char *url;
	char *windows_dld_url;
};

int phone_home_init(void);
void phone_home_terminate(void);

Release * release_new(void);
Release * release_get_latest(void);
void release_dispose(Release *_this);

#endif /* __PHONE_HOME_H__ */
