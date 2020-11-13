idf_component_register(SRCS "mesh_light.c"
                            "mesh_main.c"
			    "dht11.c"
                    INCLUDE_DIRS "." "include"
	            EMBED_TXTFILES howsmyssl_com_root_cert.pem)
