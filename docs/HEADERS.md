# Les en-têtes HTTP — catalogue de référence

> Ce document est une **encyclopédie de consultation** des en-têtes (headers) HTTP : à quoi ils servent, **toutes** les options/directives qu'un client ou un serveur peut y mettre, avec des exemples.
> Il complète `HTTP_REQUESTS.md` (qui, lui, raconte la théorie du protocole et dissèque notre parseur). Ici on ne parle pas d'états ni de `recv` : on ouvre le dictionnaire des headers, catégorie par catégorie.
> Chaque section se termine par un encadré **« Pour notre serveur »** qui dit si webserv **lit**, **émet**, ou **ignore** le header, et si c'est du mandatory, du bonus, ou hors-sujet.

---

## 0. Rappels de format (valables partout)

- Un header = `Nom: valeur`, une paire par ligne, terminée par `\r\n` (CRLF).
- Le **nom est insensible à la casse** : `Host`, `host`, `HOST` sont identiques. (Chez nous : rangés en minuscules, cf. `HTTP_REQUESTS.md` §17.6.)
- La **valeur** peut contenir des espaces internes ; seuls les espaces/tabs de bordure (OWS) sont retirés.
- Certains headers acceptent **plusieurs valeurs** séparées par des virgules (`Accept: a, b, c`) ; d'autres, **une liste de directives** séparées par `;` (`Set-Cookie`, `Content-Type`).
- Beaucoup de valeurs utilisent un système de **qualité** `q=` (de `0` à `1`) pour exprimer une préférence (négociation de contenu, §4).

### Sens de circulation

| Type | Qui l'envoie | Exemples |
|---|---|---|
| **Request header** | le **client** → serveur | `Host`, `Accept`, `Cookie`, `Authorization` |
| **Response header** | le **serveur** → client | `Set-Cookie`, `Location`, `ETag`, `Cache-Control` |
| **Representation header** | décrit le **corps** (des deux côtés) | `Content-Type`, `Content-Length`, `Content-Encoding` |

Un même nom peut exister dans les deux sens avec un rôle différent (ex. `Cache-Control` côté requête ≠ côté réponse — §5).

---

## 1. En-têtes de connexion et de message

Ceux qui pilotent la **connexion TCP** et le **cadrage du message** (où finit la requête/réponse).

| Header | Sens | Valeurs / options | Rôle |
|---|---|---|---|
| **Host** | requête | `nom[:port]` | le site + port visés. **Obligatoire en HTTP/1.1.** Sert à choisir le virtual host. |
| **Connection** | les deux | `keep-alive`, `close`, ou un nom de header à « hop-by-hop » | garder ou fermer la connexion ; liste les headers à ne pas relayer par un proxy. |
| **Keep-Alive** | les deux | `timeout=5, max=1000` | paramètres du keep-alive (durée d'inactivité tolérée, nb max de requêtes). Informatif. |
| **Content-Length** | les deux | un entier (octets) | taille exacte du corps. **Critique** pour savoir où il finit. |
| **Transfer-Encoding** | les deux | `chunked`, `gzip`, `compress`, `deflate` | corps découpé/encodé pour le transport. On ne gère que `chunked`. |
| **TE** | requête | `trailers`, `gzip;q=…` | dit quels transfer-encodings le client accepte **en réponse**. |
| **Trailer** | les deux | liste de noms de headers | annonce des headers placés **après** un corps chunked (dans le trailer). |
| **Upgrade** | les deux | `websocket`, `h2c`, `TLS/1.2` | demande de changer de protocole (→ `101 Switching Protocols`). |
| **Via** | les deux | `1.1 proxy1, 1.1 proxy2` | trace les proxies traversés. |
| **Date** | les deux | date RFC 7231 (`Tue, 21 Jul 2026 09:42:16 GMT`) | horodatage du message, toujours en **GMT**. |

> **Pour notre serveur** — mandatory, cœur du parseur.
> On **lit** `Host` (obligatoire 1.1, choix du vhost), `Content-Length` et `Transfer-Encoding: chunked` (cadrage du corps), `Connection` (keep-alive vs close). On **émet** `Date`, `Connection`, `Content-Length`. On **rejette** un `Transfer-Encoding` autre que `chunked` (`501`) et la présence simultanée `Content-Length` + `Transfer-Encoding` (`400`, anti-smuggling). `Upgrade`/`TE`/`Trailer`/`Via` : **ignorés** (pas de WebSocket ni de proxy au programme).

---

## 2. En-têtes de représentation (le corps)

Ils décrivent **ce qu'est** le corps transporté, indépendamment de la requête/réponse.

| Header | Valeurs / options | Rôle |
|---|---|---|
| **Content-Type** | `type/sous-type; param` — ex. `text/html; charset=utf-8`, `multipart/form-data; boundary=----X`, `application/json` | nature du corps + paramètres (charset, boundary). |
| **Content-Length** | entier | taille en octets (cf. §1). |
| **Content-Encoding** | `gzip`, `br`, `deflate`, `zstd`, `identity` | compression **de bout en bout** du contenu (le client doit décompresser). ≠ `Transfer-Encoding`. |
| **Content-Language** | `fr`, `en-US`, … | langue(s) du contenu. |
| **Content-Location** | une URL | l'emplacement canonique de la représentation renvoyée. |
| **Content-Disposition** | `inline`, `attachment; filename="x.png"`, ou (dans multipart) `form-data; name="champ"; filename="…"` | comment présenter le corps : afficher, télécharger, ou décrire une partie de formulaire. |
| **Content-Range** | `bytes 200-999/2000` | quelle tranche d'octets ce corps représente (réponses `206`, §9). |

### Le cas `Content-Type: multipart/form-data`

C'est **le** type de l'upload de fichiers. Le paramètre `boundary=…` donne la chaîne séparatrice entre les parties, chaque partie ayant son propre `Content-Disposition` (avec `name` et parfois `filename`). Détail complet dans `HTTP_REQUESTS.md` §10 et `ROUTING.md` (upload).

> **Pour notre serveur** — mandatory pour l'upload.
> On **lit** `Content-Type` (extraction du `boundary` multipart, cf. `Router/Upload`) et le `Content-Disposition` de chaque partie (le `filename`). On **émet** `Content-Type` + `Content-Length` sur chaque réponse (via `HttpResponse::setBody`, `HTTP_REQUESTS.md` §18). `Content-Encoding`/`Content-Language`/`Content-Range` : **non gérés** (on ne compresse pas, pas de négociation de langue, pas de Range — voir §9).

---

## 3. En-têtes d'identification du client

Ce que le client raconte **sur lui-même**. Aucun n'est fiable (cf. `SECURITY.md` : « ne jamais faire confiance à une donnée externe »).

| Header | Exemple | Rôle |
|---|---|---|
| **User-Agent** | `Mozilla/5.0 (Macintosh; …) Firefox/153.0` | navigateur/outil. Truffé de valeurs historiques figées, **jamais** une base de décision. |
| **Referer** | `http://localhost:8080/` | la page d'où vient la requête (faute d'orthographe historique : « Referer », pas « Referrer »). |
| **From** | `admin@exemple.com` | e-mail du responsable de la requête. Envoyé surtout par des **robots**, jamais par les navigateurs. |
| **DNT** | `1` | « Do Not Track » (déprécié, quasi ignoré). |
| **Sec-GPC** | `1` | « Global Privacy Control », signal de non-vente de données. |
| **Origin** | `http://localhost:8080` | l'origine (schéma+host+port) à l'initiative de la requête. Central pour CORS (§8). |

### Client Hints (`Sec-CH-UA-*`)

Remplacement moderne et modulaire du `User-Agent`, envoyé seulement si le serveur le demande (via `Accept-CH`) : `Sec-CH-UA`, `Sec-CH-UA-Platform`, `Sec-CH-UA-Mobile`, `Sec-CH-UA-Platform-Version`… Chaque champ isolé pour limiter le fingerprinting.

> **Pour notre serveur** — hors-sujet.
> **Ignorés** intégralement. On peut les **logguer** pour le debug, jamais s'en servir pour router ou décider quoi que ce soit. (Le `User-Agent` reflété dans une réponse serait une faille de type reflected-XSS — cf. l'ancien body de debug retiré.)

---

## 4. Négociation de contenu (le client dit ce qu'il préfère)

Le client annonce ses **préférences** ; le serveur, s'il le peut, choisit la meilleure représentation et l'indique via `Vary`.

| Header (requête) | Exemple | Le client accepte… |
|---|---|---|
| **Accept** | `text/html, application/xhtml+xml, image/*;q=0.8, */*;q=0.5` | quels **types MIME**, avec préférences `q`. |
| **Accept-Language** | `fr, fr-FR;q=0.9, en;q=0.7` | quelles **langues**. |
| **Accept-Encoding** | `gzip, deflate, br, zstd` | quelles **compressions** de contenu. |
| **Accept-Charset** | `utf-8, iso-8859-1;q=0.5` | quels **jeux de caractères** (déprécié, utf-8 partout). |

| Header (réponse) | Rôle |
|---|---|
| **Vary** | `Accept-Encoding, Accept-Language` : dit **sur quels headers de requête** la réponse a varié → indispensable aux caches pour ne pas servir la version FR à un client EN. |

### Le système `q=` (quality values)

Chaque valeur peut porter un poids `;q=` entre `0` (refusé) et `1` (préféré, défaut). Le serveur classe et choisit. `*/*;q=0.5` = « à défaut, n'importe quoi, mais je préfère le reste ».

> **Pour notre serveur** — hors-sujet mandatory.
> On **ignore** toute négociation : on sert le fichier tel qu'il est sur le disque, avec le MIME déduit de son extension (`Router/MimeTypes`). On ne fait ni compression (`Accept-Encoding`), ni multilingue (`Accept-Language`), donc **pas** de `Vary`. C'est conforme au sujet.

---

## 5. Le cache (contrôle de la fraîcheur) — section détaillée

C'est le cœur de ta question. Le cache HTTP répond à : *« puis-je réutiliser une réponse déjà obtenue au lieu de refaire un aller-retour ? »*. Deux grandes familles de headers : ceux qui **fixent une durée de vie** (fraîcheur) et ceux qui permettent de **revalider** une copie périmée (requêtes conditionnelles).

### 5.1 `Cache-Control` — le chef d'orchestre

Une **liste de directives** séparées par des virgules. Attention : les directives **de requête** et **de réponse** sont différentes.

**Directives de réponse** (serveur → dit comment sa réponse peut être cachée) :

| Directive | Effet |
|---|---|
| `public` | cacheable par **n'importe quel** cache (navigateur **et** proxies partagés). |
| `private` | cacheable **uniquement** par le cache privé du navigateur, pas par un proxy partagé. |
| `no-cache` | peut être stocké, mais **doit être revalidé** auprès du serveur avant chaque réutilisation. |
| `no-store` | **interdiction totale** de stocker (rien sur disque, rien en mémoire). Le plus strict. |
| `max-age=N` | fraîche pendant **N secondes** (à partir de la génération). |
| `s-maxage=N` | comme `max-age` mais **pour les caches partagés** uniquement (prioritaire sur `max-age` chez eux). |
| `must-revalidate` | une fois périmée, **interdit** de servir la copie stale ; revalidation obligatoire. |
| `proxy-revalidate` | idem, mais seulement pour les caches partagés. |
| `no-transform` | interdit à un proxy de modifier le contenu (recompression d'images…). |
| `immutable` | le contenu **ne changera jamais** pendant sa fraîcheur → le navigateur ne revalide même pas sur rechargement. Idéal pour les assets versionnés (`app.a1b2c3.js`). |
| `stale-while-revalidate=N` | autorise à servir une copie périmée pendant N s **le temps** de revalider en arrière-plan. |
| `stale-if-error=N` | autorise à servir une copie périmée pendant N s **si le serveur est en erreur**. |

**Directives de requête** (client → contraint le cache pour cette requête) :

| Directive | Effet |
|---|---|
| `no-cache` | force la revalidation (ce que fait un `Ctrl+Maj+R`). |
| `no-store` | ne stocke pas cette réponse. |
| `max-age=N` | « je veux une copie de moins de N secondes ». |
| `max-stale[=N]` | « j'accepte une copie périmée (jusqu'à N s de retard) ». |
| `min-fresh=N` | « je veux une copie qui reste fraîche encore au moins N s ». |
| `no-transform` | pas de transformation par les intermédiaires. |
| `only-if-cached` | « réponds seulement si tu as une copie en cache, sinon `504` » (mode hors-ligne). |

Exemples typiques :

```
Cache-Control: no-store                      # données sensibles : jamais de cache
Cache-Control: no-cache                       # cacheable mais revalider à chaque fois
Cache-Control: public, max-age=3600           # cacheable 1 h par tout le monde
Cache-Control: public, max-age=31536000, immutable   # asset versionné : 1 an, jamais revalidé
Cache-Control: private, max-age=0, must-revalidate    # page perso, toujours revalidée
```

### 5.2 Les autres headers de fraîcheur

| Header | Sens | Rôle |
|---|---|---|
| **Expires** | réponse | date d'expiration absolue (`Expires: Wed, 21 Jul 2027 07:28:00 GMT`). **Ancien** mécanisme, supplanté par `Cache-Control: max-age` (prioritaire si les deux sont présents). |
| **Pragma** | requête | `Pragma: no-cache` — vestige HTTP/1.0, équivalent grossier de `Cache-Control: no-cache`. Rétrocompatibilité. |
| **Age** | réponse | depuis combien de secondes la réponse est en cache (ajouté par les proxies). |

### 5.3 La revalidation conditionnelle (validators + `If-*`)

Quand une copie est périmée, plutôt que de tout re-télécharger, le client demande : *« a-t-elle changé ? »*. Le serveur répond **`304 Not Modified`** (sans corps, très léger) si rien n'a bougé, ou `200` avec le nouveau contenu sinon.

**Validators émis par le serveur :**

| Header (réponse) | Exemple | Rôle |
|---|---|---|
| **ETag** | `ETag: "a1b2c3"` ou `W/"a1b2c3"` (weak) | empreinte opaque de la version du contenu. Le plus précis. |
| **Last-Modified** | `Last-Modified: Tue, 21 Jul 2026 09:00:00 GMT` | date de dernière modification (granularité 1 s). |

**Conditions posées par le client** (dans la requête suivante) :

| Header (requête) | Se base sur | Sémantique |
|---|---|---|
| **If-None-Match** | `ETag` | « renvoie le contenu **seulement si** l'ETag a changé » → sinon `304`. (Usage cache le plus courant.) |
| **If-Modified-Since** | `Last-Modified` | « … seulement si modifié depuis cette date » → sinon `304`. |
| **If-Match** | `ETag` | « agis **seulement si** l'ETag correspond encore » → sinon `412 Precondition Failed`. Sert aux écritures concurrentes (éviter d'écraser). |
| **If-Unmodified-Since** | `Last-Modified` | « … seulement si **pas** modifié depuis » → sinon `412`. |
| **If-Range** | `ETag`/date | pour les reprises de téléchargement (§9) : ne continue que si la ressource n'a pas changé. |

Cycle typique (revalidation par ETag) :

```
1)  GET /style.css                    →  200 OK, ETag: "v7", Cache-Control: max-age=60
    (60 s plus tard, copie périmée)
2)  GET /style.css                       Requête : If-None-Match: "v7"
    (rien n'a changé)                  →  304 Not Modified   (aucun corps, ultra-léger)
```

> **Pour notre serveur** — hors mandatory, mais **très pertinent** (cf. ta question favicon).
> Aujourd'hui webserv **n'émet aucun header de cache** : ni `Cache-Control`, ni `Expires`, ni `ETag`, ni `Last-Modified`. Conséquence directe de ton bug favicon : le navigateur, faute d'instructions, applique un **cache heuristique** et ressert une vieille copie.
> Deux améliorations possibles (bonus / confort de test, **non exigées par le sujet**) :
> 1. **`Cache-Control: no-store`** sur les réponses → garantit que le navigateur ne cache jamais pendant tes tests (fini le favicon fantôme).
> 2. **`Last-Modified` + `ETag`** sur les fichiers statiques (à partir du `stat` déjà fait dans `Router::serveFile`) + gestion de `If-None-Match`/`If-Modified-Since` → répondre `304`. C'est ce que fait NGINX. Beau bonus, mais à ne creuser qu'une fois le mandatory parfait.

---

## 6. Les cookies (état côté client) — section détaillée

HTTP est **sans état** : chaque requête est indépendante. Les cookies rétablissent une continuité (session, préférences). Le serveur **pose** un cookie, le navigateur le **renvoie** à chaque requête vers le même domaine.

### 6.1 `Set-Cookie` (réponse : le serveur pose un cookie)

Un `Set-Cookie` par cookie. Format : `Nom=valeur` suivi d'attributs séparés par `;`.

```
Set-Cookie: session=abc123; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=3600
```

**Tous les attributs :**

| Attribut | Valeur | Effet |
|---|---|---|
| *(le couple)* | `nom=valeur` | la donnée. La valeur est opaque (souvent un ID de session, pas la donnée elle-même). |
| **Expires** | date GMT | date d'expiration absolue. Sans `Expires` **ni** `Max-Age` → **cookie de session** (effacé à la fermeture du navigateur). |
| **Max-Age** | secondes | durée de vie relative. **Prioritaire** sur `Expires`. `Max-Age=0` (ou négatif) → supprime le cookie. |
| **Domain** | `exemple.com` | domaine qui recevra le cookie (inclut les sous-domaines). Absent → l'host exact seulement. |
| **Path** | `/`, `/app` | chemin sous lequel le cookie est envoyé. `/` = tout le site. |
| **Secure** | *(drapeau)* | envoyé **uniquement en HTTPS**. |
| **HttpOnly** | *(drapeau)* | **inaccessible au JavaScript** (`document.cookie`) → protège du vol par XSS. |
| **SameSite** | `Strict` \| `Lax` \| `None` | contrôle l'envoi en **contexte cross-site** (anti-CSRF). Détail ci-dessous. |
| **Partitioned** | *(drapeau)* | cookie cloisonné par site top-level (CHIPS), anti-tracking tiers. Récent. |

**`SameSite` en détail** (défense CSRF clé) :

| Valeur | Le cookie est envoyé… |
|---|---|
| `Strict` | **jamais** en cross-site (même en cliquant un lien externe vers le site). Max sécurité, un peu rude pour l'UX. |
| `Lax` | en navigation **top-level** (clic sur un lien) mais pas sur les requêtes tierces (images, iframes, POST cross-site). **Défaut** des navigateurs modernes. |
| `None` | **toujours**, y compris cross-site — mais **exige `Secure`** (HTTPS obligatoire). Pour les cookies tiers légitimes (SSO, widgets). |

**Préfixes de nom** (sécurité renforcée, imposés par le navigateur) :

- `__Secure-` : le cookie **doit** avoir `Secure` (ex. `__Secure-session=…`).
- `__Host-` : **doit** avoir `Secure`, **doit** avoir `Path=/`, et **ne doit pas** avoir `Domain` → verrouille le cookie à l'host exact. Le plus sûr.

### 6.2 `Cookie` (requête : le navigateur renvoie les cookies)

Le client renvoie **tous** les cookies applicables, concaténés sur **une seule ligne**, séparés par `; ` :

```
Cookie: session=abc123; theme=dark; lang=fr
```

Note : à l'aller (`Cookie`), **pas d'attributs** — juste les paires `nom=valeur`. Les attributs (`Path`, `HttpOnly`…) ne servent qu'au navigateur pour décider quoi renvoyer, ils ne repartent jamais.

> **Pour notre serveur** — **bonus implémenté ✅** (cookies + sessions, cf. sujet §V bonus).
>
> **Côté requête** : rien à ajouter. `HttpRequest` met les noms en minuscules et **rejette les caractères de contrôle** dans les valeurs (400), donc `cookie` arrive validé ; `isExcludedFromEnv` le laisse passer (pas hop-by-hop, pas de `_` dans le nom) et le CGI reçoit `HTTP_COOKIE` tel quel. Le découpage sur `; ` est fait par le script — c'est sa responsabilité, pas celle du serveur.
>
> **Côté réponse** : `HttpResponse::_headers` est passé de `std::map` à **`std::multimap`**, précisément parce qu'un `Set-Cookie` **ne peut pas** être fusionné avec un autre. La règle générale (RFC 7230 §3.2.2) autorise à joindre deux en-têtes de même nom par une virgule *si et seulement si* la grammaire de leur valeur est une liste séparée par des virgules ; `Set-Cookie` est **l'exception nommée** par la RFC, car sa valeur contient déjà des virgules (`Expires=Wed, 21 Oct 2026 …`). Joindre par `;` ne marche pas davantage : dans `Set-Cookie`, le `;` sépare les **attributs d'un seul cookie**, pas les cookies entre eux (l'inverse exact de la ligne `Cookie` du §6.2 — c'est la confusion classique). Un en-tête `Set-Cookie` = **exactement un cookie**, par construction ; N cookies = N lignes.
>
> D'où le découpage :
> - `setHeader(k, v)` → `erase(k)` puis `insert` : sémantique « remplace », inchangée pour les ~20 appelants existants (Router, Client, CGI) ;
> - `addHeader(k, v)` → `insert` seul : sémantique « ajoute », empruntée par `Set-Cookie` uniquement ;
> - `serialize()` est **inchangé** : parcourir un `multimap` visite chaque élément, donc une ligne par entrée sort naturellement.
>
> `Cgi::buildResponse` route sur `lkey == "set-cookie"` → `addHeader`, tout le reste → `setHeader`. Pas de normalisation de casse nécessaire : le routage lit déjà `lkey` en minuscules et les noms d'en-tête sont insensibles à la casse sur le fil.
>
> **La session** est tenue par le script CGI, pas par le serveur : `www/cgi-bin/session.py` (cf. `docs/CGI.md` §9ter) génère un ID via `os.urandom`, le pose en `Set-Cookie: session_id=…; Path=/; HttpOnly; SameSite=Lax`, et garde l'état (compteur de visites, pseudo) dans `sessions.txt`. Seul l'identifiant circule. C'est la bonne répartition : le serveur transporte des en-têtes, l'application tient l'état.
>
> **Piège documenté** : sans attribut `Path`, la portée par défaut d'un cookie est le **répertoire de l'URL courante** — soit `/cgi-bin` pour nos scripts. Le cookie ne serait alors jamais renvoyé au reste du site. Tous nos cookies portent `Path=/`, à la création **comme à la suppression** (sinon `Max-Age=0` crée un second cookie au lieu d'écraser le premier).

---

## 7. Authentification

| Header | Sens | Exemple | Rôle |
|---|---|---|---|
| **WWW-Authenticate** | réponse (`401`) | `Basic realm="zone"`, `Bearer` | le serveur réclame une authentification et annonce le **schéma**. |
| **Authorization** | requête | `Basic dXNlcjpwYXNz`, `Bearer eyJhbGci…` | les identifiants du client. |
| **Proxy-Authenticate** | réponse (`407`) | idem, pour un **proxy** | le proxy réclame une auth. |
| **Proxy-Authorization** | requête | idem | identifiants pour le proxy. |

**Schémas courants :** `Basic` (login:mot de passe en base64 — **pas** du chiffrement, à réserver au HTTPS), `Bearer` (jeton, ex. OAuth/JWT), `Digest` (défi-réponse hashé).

> **Pour notre serveur** — hors-sujet. **Ignorés** (pas d'authentification au programme webserv).

---

## 8. Sécurité et CORS (réponses)

Headers que le serveur émet pour **durcir** le navigateur. (Ta config globale `web/security.md` les détaille pour un vrai front ; ici, résumé.)

| Header | Exemple | Rôle |
|---|---|---|
| **Strict-Transport-Security** (HSTS) | `max-age=31536000; includeSubDomains; preload` | force HTTPS pour les prochaines visites. |
| **Content-Security-Policy** (CSP) | `default-src 'self'; script-src 'self'` | liste blanche des sources autorisées → anti-XSS. |
| **X-Content-Type-Options** | `nosniff` | interdit au navigateur de deviner le MIME (anti-confusion de type). |
| **X-Frame-Options** | `DENY`, `SAMEORIGIN` | anti-clickjacking (interdit l'affichage en iframe). |
| **Referrer-Policy** | `strict-origin-when-cross-origin` | contrôle ce qui fuit dans le `Referer`. |
| **Permissions-Policy** | `camera=(), microphone=()` | désactive des API navigateur. |
| **Cross-Origin-Resource-Policy** (CORP) | `same-origin` | qui peut charger cette ressource. |

**CORS** (partage de ressources entre origines) — quand une page d'une origine appelle une autre origine :

| Header | Sens | Rôle |
|---|---|---|
| **Origin** | requête | l'origine appelante (§3). |
| **Access-Control-Allow-Origin** | réponse | origine(s) autorisée(s) : `*` ou une origine précise. |
| **Access-Control-Allow-Methods** | réponse | méthodes autorisées en cross-origin (préflight `OPTIONS`). |
| **Access-Control-Allow-Headers** | réponse | headers custom autorisés. |
| **Access-Control-Allow-Credentials** | réponse | `true` : autorise l'envoi des cookies cross-origin. |
| **Access-Control-Max-Age** | réponse | durée de cache du préflight. |

> **Pour notre serveur** — hors mandatory. **Non émis.** Serveur de fichiers local sans notion d'origines multiples ni de front applicatif. On pourrait ajouter `X-Content-Type-Options: nosniff` en durcissement gratuit (cf. `SECURITY.md`), mais rien n'est exigé.

---

## 9. Requêtes partielles (Range) et redirection

### Range (téléchargement partiel / reprise)

| Header | Sens | Exemple | Rôle |
|---|---|---|---|
| **Accept-Ranges** | réponse | `bytes`, `none` | le serveur annonce s'il gère les tranches. |
| **Range** | requête | `bytes=0-1023`, `bytes=1024-` | le client demande une **tranche** d'octets (reprise, streaming). |
| **Content-Range** | réponse (`206`) | `bytes 0-1023/5000` | quelle tranche est renvoyée, sur quelle taille totale. |
| **If-Range** | requête | ETag ou date | ne renvoie la suite que si la ressource n'a pas changé (§5.3), sinon renvoie tout. |

Codes associés : **`206 Partial Content`** (tranche livrée), **`416 Range Not Satisfiable`** (tranche hors limites).

### Redirection et divers

| Header | Sens | Rôle |
|---|---|---|
| **Location** | réponse | l'URL cible d'une redirection (`301`/`302`/`307`/`308`) ou d'un `201 Created`. |
| **Retry-After** | réponse (`503`/`429`) | `120` (secondes) ou une date : quand réessayer. |
| **Allow** | réponse (`405`) | liste des méthodes autorisées sur la ressource (`Allow: GET, POST`). |
| **Server** | réponse | logiciel serveur (`Server: webserv`). |

> **Pour notre serveur** — partiellement mandatory.
> On **émet** `Location` (redirections `301`/`302`, `Router::makeRedirect`), `Allow` (sur `405`, `Router::run`), et `Server: webserv` (sur toutes les réponses). On **ne gère PAS** `Range`/`Accept-Ranges`/`Content-Range` (pas de téléchargement partiel — hors sujet ; on sert le fichier entier). `Retry-After` : non émis.

---

## 10. Fetch Metadata (`Sec-Fetch-*`) — le contexte de la requête

Envoyés automatiquement par les navigateurs modernes pour dire **dans quel contexte** une requête est faite (défense contre CSRF/XSSI côté serveur averti). Tu les as vus dans ta requête favicon.

| Header | Valeurs | Dit quoi |
|---|---|---|
| **Sec-Fetch-Dest** | `document`, `image`, `script`, `style`, `font`, `empty`… | à quoi sert la ressource demandée (ex. `image` pour un favicon). |
| **Sec-Fetch-Mode** | `navigate`, `cors`, `no-cors`, `same-origin` | le mode de la requête. |
| **Sec-Fetch-Site** | `same-origin`, `same-site`, `cross-site`, `none` | relation entre l'origine de la page et la cible. |
| **Sec-Fetch-User** | `?1` | présent si l'utilisateur a **déclenché** la navigation (clic), pas un script. |

Exemple (ton favicon) : `Sec-Fetch-Dest: image`, `Sec-Fetch-Mode: no-cors`, `Sec-Fetch-Site: same-origin` → « le navigateur charge une image, en same-origin, sans CORS ». Un serveur durci pourrait refuser un `script` demandé en `Sec-Fetch-Dest: image`.

> **Pour notre serveur** — hors-sujet. **Ignorés** (loggables pour le debug). Un vrai serveur de sécurité s'en servirait ; nous, non.

---

## 11. En-têtes de proxy / transfert

Ajoutés par les reverse-proxies/load-balancers en amont. Utiles à connaître, jamais fiables sans proxy de confiance.

| Header | Exemple | Rôle |
|---|---|---|
| **X-Forwarded-For** | `203.0.113.7, 70.41.3.18` | chaîne des IP clientes traversées. |
| **X-Forwarded-Proto** | `https` | protocole d'origine (avant le proxy). |
| **X-Forwarded-Host** | `exemple.com` | `Host` d'origine. |
| **X-Real-IP** | `203.0.113.7` | IP cliente réelle (variante nginx). |
| **Forwarded** | `for=203.0.113.7; proto=https; host=exemple.com` | version **standardisée** (RFC 7239) des `X-Forwarded-*`. |

> **Pour notre serveur** — hors-sujet. **Ignorés.** On est le serveur d'origine, pas derrière un proxy dans le cadre du projet.

---

## 12. Synthèse — ce que webserv fait de chaque famille

| Famille | Lit (requête) | Émet (réponse) | Statut |
|---|---|---|---|
| Connexion / cadrage (§1) | `Host`, `Content-Length`, `Transfer-Encoding`, `Connection` | `Date`, `Content-Length`, `Connection`, `Server` | **mandatory ✅** |
| Représentation (§2) | `Content-Type` (boundary multipart), `Content-Disposition` | `Content-Type`, `Content-Length` | **mandatory ✅** |
| `Expect` (→ `HTTP_REQUESTS.md` §6) | `Expect: 100-continue` | `100 Continue` | **mandatory ✅** |
| Identification client (§3) | *(loggable)* | — | ignoré |
| Négociation de contenu (§4) | — | — | hors-sujet |
| **Cache (§5)** | — | — | **absent → cause du bug favicon ; bonus possible (`no-store`, `ETag`/`304`)** |
| **Cookies (§6)** | `Cookie` (→ `HTTP_COOKIE`) | `Set-Cookie` (multiples) | **bonus ✅ (sessions via CGI)** |
| Authentification (§7) | — | — | hors-sujet |
| Sécurité / CORS (§8) | — | — | hors-sujet (`nosniff` en durcissement optionnel) |
| Range / redirection (§9) | — | `Location`, `Allow`, `Server` | **redirection/`405` mandatory ✅** ; Range non géré |
| Fetch metadata (§10) | *(loggable)* | — | ignoré |
| Proxy (§11) | — | — | hors-sujet |

**À retenir** : le mandatory webserv ne touche qu'une **poignée** de headers (cadrage du message + service de fichiers). Tout le reste — cache, cookies, sécurité, CORS, négociation — est soit **bonus** (cookies), soit **hors périmètre** (le navigateur les gère, nous les ignorons proprement). Mais les **ignorer proprement** fait partie de la robustesse : un header inconnu ne doit jamais faire planter le parseur (il atterrit dans la map et n'est simplement pas lu).

---

## 13. Références

- **MDN — Liste complète des en-têtes** (le meilleur dictionnaire, en français) : https://developer.mozilla.org/fr/docs/Web/HTTP/Headers
- **MDN — HTTP caching** (la section cache, très claire) : https://developer.mozilla.org/fr/docs/Web/HTTP/Caching
- **MDN — Set-Cookie** : https://developer.mozilla.org/fr/docs/Web/HTTP/Headers/Set-Cookie
- **RFC 9110** — HTTP Semantics (définit la plupart des headers) : https://www.rfc-editor.org/rfc/rfc9110
- **RFC 9111** — HTTP Caching (`Cache-Control`, validators, `304`) : https://www.rfc-editor.org/rfc/rfc9111
- **RFC 6265** — Cookies (`Set-Cookie`, `Cookie`, attributs) : https://www.rfc-editor.org/rfc/rfc6265
- **RFC 7239** — Forwarded (proxies) : https://www.rfc-editor.org/rfc/rfc7239

Conseil : pour un header précis, tape « MDN <NomDuHeader> » — chaque page MDN liste **toutes** les valeurs possibles avec des exemples. Les RFC ne servent qu'à trancher un détail de syntaxe.
