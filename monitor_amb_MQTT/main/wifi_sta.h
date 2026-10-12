#pragma once

/* Inicializa o Wi-Fi em modo estacao e bloqueia ate obter IP.
 * Reconecta automaticamente se a conexao cair. */
void wifi_sta_connect(const char *ssid, const char *password);
