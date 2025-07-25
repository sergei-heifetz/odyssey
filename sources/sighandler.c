/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <odyssey.h>

static inline od_retcode_t
od_system_gracefully_killer_invoke(od_system_t *system)
{
	od_instance_t *instance = system->global->instance;
	if (instance->shutdown_worker_id != INVALID_COROUTINE_ID) {
		return OK_RESPONSE;
	}
	int64_t mid;
	mid = machine_create("shutdowner", od_grac_shutdown_worker, system);
	if (mid == -1) {
		od_error(&instance->logger, "gracefully_killer", NULL, NULL,
			 "failed to invoke gracefully killer coroutine");
		return NOT_OK_RESPONSE;
	}
	instance->shutdown_worker_id = mid;

	return OK_RESPONSE;
}

static inline void od_system_cleanup(od_system_t *system)
{
	od_instance_t *instance = system->global->instance;
	od_list_t *i;

	od_list_foreach(&instance->config.listen, i)
	{
		od_config_listen_t *listen;
		listen = od_container_of(i, od_config_listen_t, link);
		if (listen->host)
			continue;
		/* remove unix socket files */
		char path[PATH_MAX];
		od_snprintf(path, sizeof(path), "%s/.s.PGSQL.%d",
			    instance->config.unix_socket_dir, listen->port);
		unlink(path);
	}
}

od_attribute_noreturn() void od_system_shutdown(od_system_t *system,
						od_instance_t *instance)
{
	od_worker_pool_t *worker_pool;

	worker_pool = system->global->worker_pool;
	od_log(&instance->logger, "system", NULL, NULL,
	       "SIGINT received, shutting down");

	// lock here
	od_cron_stop(system->global->cron);

	od_worker_pool_stop(worker_pool);

	/* Prevent OpenSSL usage during deinitialization */
	od_worker_pool_wait();

	od_extension_free(&instance->logger, system->global->extensions);

#ifdef OD_SYSTEM_SHUTDOWN_CLEANUP
	od_router_free(system->global->router);

	od_system_cleanup(system);

	/* stop machinaruim and free */
	od_instance_free(instance);
#endif
	od_logger_shutdown(&instance->logger);

	exit(0);
}

void od_system_signal_handler(void *arg)
{
	od_system_t *system = arg;
	od_instance_t *instance = system->global->instance;

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, OD_SIG_LOG_ROTATE);
	sigaddset(&mask, OD_SIG_GRACEFUL_SHUTDOWN);

	sigset_t ignore_mask;
	sigemptyset(&ignore_mask);
	sigaddset(&ignore_mask, SIGPIPE);
	int rc;
	rc = machine_signal_init(&mask, &ignore_mask);
	if (rc == -1) {
		od_error(&instance->logger, "system", NULL, NULL,
			 "failed to init signal handler");
		return;
	}

	int term_count = 0;

	for (;;) {
		rc = machine_signal_wait(UINT32_MAX);
		if (rc == -1)
			break;
		switch (rc) {
		case SIGTERM:
		case SIGINT:
			if (++term_count >=
			    instance->config.max_sigterms_to_die) {
				exit(1);
			}

			od_system_gracefully_killer_invoke(system);
			break;
		case SIGHUP:
			od_log(&instance->logger, "system", NULL, NULL,
			       "SIGHUP received");
			od_system_config_reload(system);
			break;
		case OD_SIG_LOG_ROTATE:
			if (instance->config.log_file) {
				od_log(&instance->logger, "system", NULL, NULL,
				       "SIGUSR1 received, reopening log");
				rc = od_logger_reopen(
					&instance->logger,
					instance->config.log_file);
				if (rc == -1) {
					od_error(
						&instance->logger, "system",
						NULL, NULL,
						"failed to reopen log file '%s'",
						instance->config.log_file);
				} else {
					od_log(&instance->logger, "system",
					       NULL, NULL, "log reopened");
				}
			}
			break;
		case OD_SIG_GRACEFUL_SHUTDOWN:
			if (instance->config.enable_online_restart_feature ||
			    instance->config.graceful_die_on_errors) {
				od_log(&instance->logger, "system", NULL, NULL,
				       "SIG_GRACEFUL_SHUTDOWN received");
				od_system_gracefully_killer_invoke(system);
			} else {
				od_log(&instance->logger, "system", NULL, NULL,
				       "SIGUSR2 received, but online restart feature not "
				       "enabled, doing nothing");
			}
			break;
		}
	}
}