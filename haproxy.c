// Job done by https://twitter.com/lightplay8 to help me :-) Thx to him


/*
![haproxy](https://d1q6f0aelx0por.cloudfront.net/product-logos/74753c28-31a5-46fa-88c1-5336bdb2e989-haproxy.png)

### __Disclaimer__ : Le miror sur github est pas à jour. Faut aller sur le [site](http://www.hapoxy.org/)

Ca marche par un systèmes master/worker
Lorsqu'on relance haproxy pour avoir la nouvelle configuration.
Il va regarder si il ya pas déjà des anciens processus et les tuers avec un signal: `SIGUSER2` (`soft_stop` qu'il appelle dans le code).



/*
### Kill/start des processs (fichier `haproxy.c`)

## mworker_reload (ligne 602)

/*
 * When called, this function reexec haproxy with -sf followed by current
 * children PIDs and possibily old children PIDs if they didn't leave yet.
 */
static void mworker_reload()
{
	int next_argc = 0;
	int j;
	char *msg = NULL;

	mworker_block_signals();
	mworker_unregister_signals();
	setenv("HAPROXY_MWORKER_REEXEC", "1", 1);

	/* compute length  */
	while (next_argv[next_argc])
		next_argc++;

	/* 1 for haproxy -sf, 2 for -x /socket */
	next_argv = realloc(next_argv, (next_argc + 1 + 2 + global.nbproc + nb_oldpids + 1) * sizeof(char *));
	if (next_argv == NULL)
		goto alloc_error;


	/* add -sf <PID>*  to argv */
	if (children || nb_oldpids > 0)
		next_argv[next_argc++] = "-sf";
	if (children) {
		for (j = 0; j < global.nbproc; next_argc++,j++) {
			next_argv[next_argc] = memprintf(&msg, "%d", children[j]);
			if (next_argv[next_argc] == NULL)
				goto alloc_error;
			msg = NULL;
		}
	}
	/* copy old process PIDs */
	for (j = 0; j < nb_oldpids; next_argc++,j++) {
		next_argv[next_argc] = memprintf(&msg, "%d", oldpids[j]);
		if (next_argv[next_argc] == NULL)
			goto alloc_error;
		msg = NULL;
	}
	next_argv[next_argc] = NULL;

	/* add the -x option with the stat socket */
	if (cur_unixsocket) {

		next_argv[next_argc++] = "-x";
		next_argv[next_argc++] = (char *)cur_unixsocket;
		next_argv[next_argc++] = NULL;
	}

	deinit(); /* we don't want to leak FD there */
	Warning("Reexecuting Master process\n");
	execv(next_argv[0], next_argv);  <-----------------------------------------

alloc_error:
	Warning("Cannot allocate memory\n");
	Warning("Failed to reexecute the master processs [%d]\n", pid);
	return;
}

/*
## mworker_wait (ligne 605)

/*
 * Wait for every children to exit
 */
static void mworker_wait() (ligne 605)
{
	int exitpid = -1;
	int status = 0;

	mworker_register_signals();
	mworker_unblock_signals();

	while (1) {

		while (((exitpid = wait(&status)) == -1) && errno == EINTR) {
			int sig = caught_signal;
			if (sig == SIGUSR2 || sig == SIGHUP) {
				mworker_reload();
			} else {
				Warning("Exiting Master process...\n");
				mworker_kill(sig);  <--- Ca fait des kill sévères ici comme moi sur Smashbros ;)
				mworker_unregister_signals();
			}
			caught_signal = 0;
		}
	......
}

/*
### mworker_kill: tue tous les anciens process

/*
 * Send signal to every known children.
 */
static void mworker_kill(int sig)
{
	int i;

	tell_old_pids(sig);
	if (children) {
		for (i = 0; i < global.nbproc; i++)
			kill(children[i], sig);
	}
}

/*
### main() de haproxy.c (ligne 2634)
Lance les nouveux process avec la nouvelle config (voir en plus bas comment la conf est lue)
.........
/* the father launches the required number of processes */
for (proc = 0; proc < global.nbproc; proc++) {
	ret = fork();
	if (ret < 0) {
		Alert("[%s.main()] Cannot fork.\n", argv[0]);
		protocol_unbind_all();
		exit(1); /* there has been an error */
	}
	else if (ret == 0) /* child breaks here */
		break;
	children[proc] = ret;
	if (pidfd >= 0) {
		char pidstr[100];
		snprintf(pidstr, sizeof(pidstr), "%d\n", ret);
		shut_your_big_mouth_gcc(write(pidfd, pidstr, strlen(pidstr))); <--------------- best method name ever
	}
	relative_pid++; /* each child will get a different one */
}
.........

/*
Il essaye aussi de __réutiliser__ les  `socket` déjà ouvertes par les anciens `fork`.
``` c
setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
```

## Configuration: cfgparse.c (ligne 9925)
Ca utilise un `constructor` gcc pour parser les fichiers de configs.

*/
__attribute__((constructor))
static void cfgparse_init(void)
{
	/* Register internal sections */
	cfg_register_section("listen",         cfg_parse_listen,    NULL);
	cfg_register_section("frontend",       cfg_parse_listen,    NULL);
	cfg_register_section("backend",        cfg_parse_listen,    NULL);
	cfg_register_section("defaults",       cfg_parse_listen,    NULL);
	cfg_register_section("global",         cfg_parse_global,    NULL);
	cfg_register_section("userlist",       cfg_parse_users,     NULL);
	cfg_register_section("peers",          cfg_parse_peers,     NULL);
	cfg_register_section("mailers",        cfg_parse_mailers,   NULL);
	cfg_register_section("namespace_list", cfg_parse_netns,     NULL);
	cfg_register_section("resolvers",      cfg_parse_resolvers, NULL);
}

/*
## signal de kill
/*
 * upon SIGUSR1, let's have a soft stop. Note that soft_stop() broadcasts
 * a signal zero to all subscribers. This means that it's as easy as
 * subscribing to signal 0 to get informed about an imminent shutdown.
 */
static void sig_soft_stop(struct sig_handler *sh)
{
	soft_stop();
	signal_unregister_handler(sh);
	pool_gc2(NULL);
}
