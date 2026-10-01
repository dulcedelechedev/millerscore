# MillerScore em imagens

Estas capturas reais do aplicativo mostram a demonstração Golden Hour em
Score, DAW e no mixer. Consulte o [guia do usuário](index.html) para o fluxo
completo ou veja esta galeria em [inglês](../SCREENSHOTS.md).

## 1. Abra a partitura

Comece em **Score** para ler e editar a notação de Piano, Strings e Bass.

![Notação de Golden Hour em Score, com Piano, Strings e Bass](../../marketing/itchio-clean-v0.1.0/01-score-view-1600x1000.png)

## 2. Mude para DAW

Use **DAW** para examinar a mesma música no piano roll e na faixa de intensidade
(velocity). Escolha um instrumento **MS Basic/SoundFont** no inspetor de sons
para reproduzir as faixas genéricas de CC.

![Piano roll e faixa de intensidade da DAW, com o inspetor de sons MS Basic](../../marketing/itchio-clean-v0.1.0/02-piano-roll-1600x1000.png)

## 3. Adicione uma curva de CC7

Selecione a pista Piano e **CC7 — Channel Volume** para controlar o volume do
instrumento receptor da SoundFont. A curva mostrada tem três pontos; seus
valores são inteiros MIDI de 0 a 127.

![Pista Piano com uma curva de três pontos de CC7 Channel Volume e som MS Basic selecionado](../../marketing/itchio-clean-v0.1.0/03-midi-controller-lanes-1600x1000.png)

## 4. Confira o mixer durante a reprodução

Abra o **Mixer** para acompanhar os medidores das pistas e do master e ajustar
a mixagem. O CC7 genérico altera o volume do instrumento da SoundFont; o nível
do mixer é um controle separado.

![Mixer durante a reprodução, mostrando Piano, Strings, Bass e master com medidores](../../marketing/itchio-clean-v0.1.0/04-mixer-1600x1000.png)

## Limites da reprodução de controladores

As 108 faixas genéricas de CC editáveis enviam valores separados de 7 bits
(0–127) somente ao **MS Basic/SoundFont durante a reprodução principal da
partitura**. O instrumento pode ignorar controladores que não implemente;
faixas pareadas não oferecem um controle combinado automático de 14 bits.
Os 20 controladores protegidos são somente para leitura e nunca são enviados.
**Muse Sounds, VST3, saída MIDI externa e exportação de Standard MIDI File
não recebem essas faixas genéricas da DAW.** Consulte o
[guia de controladores](index.html#controllers) para o catálogo completo e as
regras de segurança.
