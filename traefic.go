/*
# [Traefik](https://github.com/containous/traefik)


Le [server.go](https://github.com/containous/traefik/blob/master/server/server.go) c'est la core partie du reverse-proxy/load-balancer engine.
*/

type Server struct {
	serverEntryPoints             serverEntryPoints
	configurationChan             chan types.ConfigMessage
	configurationValidatedChan    chan types.ConfigMessage
	signals                       chan os.Signal
	stopChan                      chan bool
	providers                     []provider.Provider
	currentConfigurations         safe.Safe
	globalConfiguration           configuration.GlobalConfiguration
	accessLoggerMiddleware        *accesslog.LogHandler
	routinesPool                  *safe.Pool
	leadership                    *cluster.Leadership
	defaultForwardingRoundTripper http.RoundTripper
	metricsRegistry               metrics.Registry
}

/*
C'est lui qui set les __providers__ à son démarrage. Ils watch la __configuration__ et renvoie toutes notifications de changement par `channel`
*/
func (server *Server) Start() {
	server.startHTTPServers()
	server.startLeadership()
	server.routinesPool.Go(func(stop chan bool) {
		server.listenProviders(stop)
	})
	server.routinesPool.Go(func(stop chan bool) {
		server.listenConfigurations(stop) <--- La ca change sa conf à chaud in memory
	})
	server.configureProviders()
	server.startProviders()  <---- Ici ca crée les watcheurs
	go server.listenSignals()
}

/*
Le reload de config se fait __in memory__ (Pas de `fork` ou autre).
*/
func (server *Server) loadConfig(configurations types.Configurations, globalConfiguration configuration.GlobalConfiguration) (map[string]*serverEntryPoint, error) {
	serverEntryPoints := server.buildEntryPoints(globalConfiguration)
	redirectHandlers := make(map[string]negroni.Handler)
	backends := map[string]http.Handler{}
	backendsHealthCheck := map[string]*healthcheck.BackendHealthCheck{}
	errorHandler := NewRecordingErrorHandler(middlewares.DefaultNetErrorRecorder{})

	for _, config := range configurations {
		frontendNames := sortedFrontendNamesForConfig(config)
	frontend:
		for _, frontendName := range frontendNames {
			frontend := config.Frontends[frontendName]

			log.Debugf("Creating frontend %s", frontendName)

			if len(frontend.EntryPoints) == 0 {
				log.Errorf("No entrypoint defined for frontend %s, defaultEntryPoints:%s", frontendName, globalConfiguration.DefaultEntryPoints)
				log.Errorf("Skipping frontend %s...", frontendName)
				continue frontend
			}

			for _, entryPointName := range frontend.EntryPoints {
				log.Debugf("Wiring frontend %s to entryPoint %s", frontendName, entryPointName)
				if _, ok := serverEntryPoints[entryPointName]; !ok {
					log.Errorf("Undefined entrypoint '%s' for frontend %s", entryPointName, frontendName)
					log.Errorf("Skipping frontend %s...", frontendName)
					continue frontend
				}
		....................................
		TL;DR
		....................................
	}
	healthcheck.GetHealthCheck().SetBackendsConfiguration(server.routinesPool.Ctx(), backendsHealthCheck)
	//sort routes
	for _, serverEntryPoint := range serverEntryPoints {
		serverEntryPoint.httpRouter.GetHandler().SortRoutes()
	}
	return serverEntryPoints, nil
}

/*
## fsnotify (bonus)

Il utilise en interne [fsnotify](https://github.com/fsnotify/fsnotify) pour poser un watcheur sur les fichiers de configuration `.toml`
Ca envois la configuration sérialisé par channel

[provider/file.go](https://github.com/containous/traefik/blob/cf508b6d481599c4ffd83c52e48beb9cf561314a/provider/file/file.go)
*/
func (p *Provider) Provide(configurationChan chan<- types.ConfigMessage, pool *safe.Pool, constraints types.Constraints) error {
	configuration, err := p.loadConfig()

	if err != nil {
		return err
	}

	if p.Watch {
		var watchItem string

		if p.Directory != "" {
			watchItem = p.Directory
		} else {
			watchItem = p.Filename
		}

		if err := p.addWatcher(pool, watchItem, configurationChan, p.watcherCallback); err != nil {
			return err
		}
	}

	sendConfigToChannel(configurationChan, configuration)
	return nil
}


## Liens vers le source code:
* [server.go](https://github.com/containous/traefik/blob/master/server/server.go)
* [main (run the server)](https://github.com/containous/traefik/blob/d89b234cadfbf8e150d8a456b05bfb5f0526c5af/cmd/traefik/traefik.go#L223)
* [file provider (watcher config)](https://github.com/containous/traefik/blob/master/provider/file/file.go)
