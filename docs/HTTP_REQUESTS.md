# Comprendre les requêtes HTTP (de zéro)

> Objectif : maîtriser ce qu'est une requête HTTP pour pouvoir écrire notre parseur sereinement.
> On part de rien. Chaque section finit par un encadré **« Pour notre serveur »** qui relie la théorie à ce qu'on devra coder.

---

## 1. Le décor : un dialogue client / serveur

HTTP, c'est juste **un dialogue en texte** entre deux programmes par-dessus une connexion TCP :

- le **client** (un navigateur, `curl`, telnet…) **envoie une requête** : « donne-moi cette page », « voici un fichier à enregistrer »…
- le **serveur** (nous !) **renvoie une réponse** : « voilà la page », « erreur 404 », « c'est enregistré »…

Un échange = **une requête → une réponse**. C'est tout. Le client parle d'abord, le serveur répond. Notre travail : lire la requête, la comprendre, fabriquer la bonne réponse.

> **Pour notre serveur** : on reçoit des octets (`recv`), on les interprète comme une requête, on renvoie des octets (`send`) qui forment une réponse. Le reactor (cf. `ARCHITECTURE.md`) gère déjà les octets ; ici on apprend à leur donner du sens.

---

## 2. Anatomie d'une requête : 4 parties

Une requête HTTP est du **texte structuré**. Toujours la même forme :

```
GET /index.html HTTP/1.1\r\n      ← (1) ligne de requête
Host: localhost:8080\r\n          ← (2) en-têtes (headers)
User-Agent: curl/8.0\r\n          ←     ... un par ligne
Accept: */*\r\n                   ←
\r\n                              ← (3) ligne VIDE = fin des en-têtes
                                  ← (4) corps (body) — vide pour un GET
```

Les quatre parties :

1. **La ligne de requête** (request line) — *quoi*, *où*, *quelle version*.
2. **Les en-têtes** (headers) — des métadonnées, une par ligne, format `Nom: valeur`.
3. **La ligne vide** — un `\r\n` tout seul qui marque la **fin des en-têtes**.
4. **Le corps** (optionnel) — les données envoyées (un fichier, un formulaire…). Absent pour un GET, présent pour un POST.

### Le détail qui compte : `\r\n`

Chaque ligne se termine par **deux caractères invisibles** : `\r` (retour chariot, *carriage return*, code 13) et `\n` (saut de ligne, *line feed*, code 10). On note ça **CRLF**.

La **fin des en-têtes** est donc un CRLF **vide** qui suit le dernier header : on obtient `\r\n\r\n` (le `\r\n` qui termine le dernier header, immédiatement suivi du `\r\n` de la ligne vide).

> **Pour notre serveur** : on accumule les octets jusqu'à voir `\r\n\r\n` → on sait que toute la « tête » (lignes 1+2) est arrivée et on peut la parser. Ce qui vient après `\r\n\r\n`, c'est le corps.

---

## 3. La ligne de requête, mot par mot

```
GET /index.html HTTP/1.1
└─┬─┘ └────┬────┘ └──┬───┘
méthode   cible   version
```

Trois champs **séparés par une espace** (un seul espace, code 32), terminés par CRLF :

- **Méthode** : l'action voulue (`GET`, `POST`, `DELETE`…).
- **Cible** (request-target) : sur quoi on agit, généralement un chemin (`/index.html`).
- **Version** : `HTTP/1.1` (ou `HTTP/1.0`).

> **Pour notre serveur** : on découpe cette première ligne sur les espaces → 3 morceaux. Si on n'a pas exactement 3 morceaux, ou une version inconnue → requête malformée → on répond `400 Bad Request`.

---

## 4. Les méthodes

La méthode dit **ce que le client veut faire**. Le sujet impose d'en gérer trois :

| Méthode | Sens | Corps ? | Notre rôle |
|---|---|---|---|
| **GET** | « donne-moi cette ressource » | non | lire un fichier et le renvoyer |
| **POST** | « voici des données, traite-les » | **oui** | recevoir un upload / un formulaire |
| **DELETE** | « supprime cette ressource » | non (en général) | supprimer un fichier |

Il en existe d'autres (`PUT`, `HEAD`, `OPTIONS`, `PATCH`…) qu'on n'a **pas** à implémenter. Si un client demande une méthode qu'une route n'autorise pas → on répond `405 Method Not Allowed`. Si la méthode n'existe pas du tout dans notre serveur → `501 Not Implemented`.

> **Pour notre serveur** : la liste des méthodes autorisées dépend de la **route** (config). Une même méthode peut être permise sur `/upload` et interdite sur `/images`.

---

## 5. La cible : chemin, query, encodage

La cible la plus courante (dite *origin-form*) ressemble à :

```
/dossier/page.html?nom=jean&age=24
└──────┬─────────┘ └───────┬──────┘
     chemin (path)      query string
```

- Le **chemin** (`path`) : où se trouve la ressource. C'est lui qu'on mappe vers un dossier disque (le `root` de la config : `/kapouet` → `/tmp/www`).
- La **query string** (après le `?`) : des paramètres `clé=valeur` séparés par `&`. Utile pour le CGI ; pour un fichier statique, on l'ignore.

### Le percent-encoding

Certains caractères ne peuvent pas apparaître tels quels dans une URL (espace, accents…). Ils sont encodés en `%` + code hexa : une espace devient `%20`, `é` devient `%C3%A9`. Décoder ça s'appelle l'**URL-decoding**.

> **Pour notre serveur** : il faudra décoder le `%XX` du chemin avant d'ouvrir un fichier. Et **attention sécurité** : un chemin comme `/../../etc/passwd` (path traversal) doit être bloqué — on ne doit jamais sortir du dossier autorisé.

---

## 6. Les en-têtes (headers)

Format : `Nom-Du-Header: valeur`, un par ligne. Le nom est **insensible à la casse** (`Host` = `host`). Voici ceux qui comptent pour nous :

| Header | À quoi ça sert | Importance webserv |
|---|---|---|
| **Host** | le nom de domaine + port visés (`localhost:8080`) | **obligatoire en HTTP/1.1** ; absent → `400` |
| **Content-Length** | taille du corps **en octets** | **critique** pour savoir où finit le corps |
| **Transfer-Encoding** | `chunked` = corps envoyé en morceaux | **critique** : alternative à Content-Length |
| **Content-Type** | le type du corps (`text/html`, `multipart/form-data`…) | indispensable pour les uploads |
| **Connection** | `keep-alive` (garder la connexion) ou `close` | gère la réutilisation de la connexion |
| **Cookie** | données de session renvoyées par le client | bonus (sessions) |
| User-Agent, Accept, Referer… | infos diverses sur le client | on peut les ignorer |

> **Pour notre serveur** : on range les headers dans une `map<string, string>` (nom → valeur), en normalisant le nom en minuscules pour gérer la casse. On ne lit vraiment que ceux dont on a besoin.

---

## 7. La ligne vide : le séparateur

Après le dernier header, une ligne **strictement vide** (`\r\n` seul) signale : « les en-têtes sont finis, ce qui suit est le corps ». C'est le fameux `\r\n\r\n`.

> **Pour notre serveur** : c'est notre **signal de fin de tête**. Tant qu'on ne l'a pas reçu, on continue d'accumuler. Garde-fou : si la tête dépasse une taille max sans qu'on voie `\r\n\r\n`, on rejette (`400`/`431`) — sinon un client malveillant peut nous faire gonfler la mémoire à l'infini.

---

## 8. Le corps : comment savoir où il finit ? (LE point clé)

Le corps n'a **pas** de marqueur de fin comme `\r\n\r\n`. Alors comment savoir quand on a tout reçu ? **Deux mécanismes, jamais les deux à la fois :**

### Cas A — `Content-Length`

Le header annonce la taille exacte :

```
POST /upload HTTP/1.1\r\n
Host: localhost:8080\r\n
Content-Type: application/x-www-form-urlencoded\r\n
Content-Length: 27\r\n
\r\n
field1=value1&field2=value2
```

Ici `Content-Length: 27` → on lit **exactement 27 octets** de corps, puis c'est fini. Simple et déterministe.

### Cas B — `Transfer-Encoding: chunked`

Le client ne connaît pas la taille totale à l'avance (ex : données générées à la volée). Il envoie le corps **par morceaux**, chacun précédé de sa taille (en hexadécimal). Voir section 9.

### Cas C — ni l'un ni l'autre

Pour une **requête**, pas de `Content-Length` ni `chunked` → **pas de corps** (typique d'un GET/DELETE).

> **Pour notre serveur** : après avoir parsé les headers, on regarde dans cet ordre :
> 1. `Transfer-Encoding: chunked` ? → on dé-chunke (section 9).
> 2. sinon `Content-Length: N` ? → on lit N octets.
> 3. sinon → pas de corps.
> C'est ça la « phase 2 » du parseur dont on parlait : lire la tête en une fois, **puis** lire le corps selon ces règles.

---

## 9. Le `chunked` en détail

Le corps est une suite de morceaux. Chaque morceau :

```
<taille en hexa>\r\n
<données>\r\n
```

et la fin est marquée par un morceau de **taille 0** :

```
0\r\n
\r\n
```

Exemple — ce corps chunked encode le mot « Wikipedia » :

```
4\r\n
Wiki\r\n
5\r\n
pedia\r\n
0\r\n
\r\n
```

Lecture : `4` (hexa) → lis 4 octets → `Wiki`. `5` → lis 5 octets → `pedia`. `0` → fini. Résultat assemblé : `Wikipedia`.

> **Pour notre serveur** : on lit la taille (hexa) jusqu'au `\r\n`, puis ce nombre d'octets, et on recommence jusqu'au chunk `0`. Le sujet précise qu'on doit **dé-chunker** avant de passer le corps au CGI (le CGI attend un corps « normal », terminé par EOF).

---

## 10. Content-Type et l'upload de fichiers

Quand un client envoie un corps, `Content-Type` dit **comment l'interpréter**. Les plus courants :

- `application/x-www-form-urlencoded` → un formulaire simple : `champ1=valeur1&champ2=valeur2`.
- `multipart/form-data` → **l'upload de fichiers** (plusieurs champs/fichiers en un seul corps).
- `application/json` → des données JSON.
- `text/plain` → du texte brut.

Le `multipart/form-data` ressemble à ça (simplifié) :

```
Content-Type: multipart/form-data; boundary=----XYZ\r\n
Content-Length: ...\r\n
\r\n
------XYZ\r\n
Content-Disposition: form-data; name="file"; filename="photo.png"\r\n
Content-Type: image/png\r\n
\r\n
<...octets bruts du fichier...>\r\n
------XYZ--\r\n
```

Le `boundary` (annoncé dans le Content-Type) est une **chaîne séparatrice** entre les parties. Chaque partie a ses propres mini-headers (dont `filename`) puis ses données.

> **Pour notre serveur** : pour l'upload, on lit le `boundary`, on découpe le corps en parties, et pour chaque partie qui a un `filename`, on écrit les données dans le dossier d'upload de la config. C'est la partie la plus fastidieuse du POST.

---

## 11. La réponse, en miroir

Comme tu parses la requête, tu fabriqueras la réponse — même structure :

```
HTTP/1.1 200 OK\r\n              ← ligne de statut (version, code, raison)
Content-Type: text/html\r\n      ← headers
Content-Length: 1024\r\n         ← (la taille de TON corps)
\r\n                             ← ligne vide
<...le contenu HTML...>          ← corps
```

La grande différence : la première ligne est une **ligne de statut** avec un **code** (200, 404…) et une phrase (`OK`, `Not Found`). Et c'est **toi** qui dois mettre le bon `Content-Length` (la taille de ce que tu renvoies).

---

## 12. Les codes de statut utiles

| Code | Sens | Quand l'utiliser |
|---|---|---|
| **200** OK | succès | GET/DELETE réussi |
| **201** Created | créé | après un upload réussi |
| **204** No Content | succès sans corps | parfois après DELETE |
| **301 / 302** | redirection (permanente / temporaire) | règle de redirection en config |
| **400** Bad Request | requête malformée | parsing impossible |
| **403** Forbidden | accès interdit | pas les droits |
| **404** Not Found | ressource absente | fichier inexistant |
| **405** Method Not Allowed | méthode interdite **sur cette route** | GET sur une route POST-only |
| **413** Payload Too Large | corps trop gros | dépasse `client_max_body_size` |
| **500** Internal Server Error | erreur serveur | bug, CGI planté |
| **501** Not Implemented | méthode inconnue | méthode non gérée |
| **505** HTTP Version Not Supported | version refusée | autre que 1.0/1.1 |

> **Pour notre serveur** : avoir le **bon** code est noté. En cas de doute, on compare avec NGINX.

---

## 13. S'entraîner pour de vrai (le plus formateur)

La théorie rentre dix fois mieux en **regardant de vraies requêtes**. À faire, dès maintenant :

**a) Voir une requête/réponse complète avec `curl` :**
```
curl -v http://example.com
```
`-v` (verbose) affiche les lignes envoyées (`>`) et reçues (`<`). Tu vois la ligne de requête, tes headers, la ligne de statut, les headers de réponse.

**b) Écrire une requête HTTP À LA MAIN avec telnet/nc :**
```
nc example.com 80
GET / HTTP/1.1
Host: example.com
(ligne vide ici)
```
Tu tapes littéralement le protocole. C'est *exactement* ce que notre serveur recevra. Magique pour comprendre.

**c) L'onglet Network du navigateur** (F12 → Network) : clique une requête, regarde « Headers » → tu vois la vraie requête que ton navigateur envoie à ton serveur.

**d) Tester contre NGINX** : lance un NGINX, envoie-lui des requêtes bizarres, observe ses réponses/codes. C'est notre référence de comportement.

---

## 14. Les bonnes références

- **MDN — HTTP** (le meilleur point de départ, clair et en français possible) :
  - Vue d'ensemble : https://developer.mozilla.org/fr/docs/Web/HTTP
  - Anatomie des messages : https://developer.mozilla.org/fr/docs/Web/HTTP/Messages
  - Méthodes : https://developer.mozilla.org/fr/docs/Web/HTTP/Methods
  - Headers : https://developer.mozilla.org/fr/docs/Web/HTTP/Headers
  - Codes de statut : https://developer.mozilla.org/fr/docs/Web/HTTP/Status
- **HTTP Made Really Easy** (tutoriel culte des étudiants webserv, va droit au but) : https://www.jmarshall.com/easy/http/
- **Les RFC officielles** (la source de vérité, en ABNF — à consulter ponctuellement, pas à lire d'un trait) :
  - RFC 9110 — HTTP Semantics (méthodes, codes, headers) : https://www.rfc-editor.org/rfc/rfc9110
  - RFC 9112 — HTTP/1.1 (le format des messages, le chunked) : https://www.rfc-editor.org/rfc/rfc9112
  - (les anciennes RFC 7230/7231 disent la même chose, encore très citées)

Conseil : commence par **MDN** pour les concepts, garde **jmarshall** sous la main pour l'implémentation, et n'ouvre les **RFC** que pour trancher un détail précis (ex : la syntaxe exacte du chunked).

---

## 15. Ce qu'il faut retenir pour notre parseur

1. Une requête = **ligne de requête** + **headers** + **ligne vide** + **corps** (optionnel).
2. Tout est en **texte**, chaque ligne finit par **`\r\n`**.
3. La **tête** finit à `\r\n\r\n` → on accumule jusque-là, puis on parse en une fois.
4. La **ligne de requête** = `MÉTHODE cible VERSION`, séparés par des espaces.
5. Les **headers** = `Nom: valeur`, nom insensible à la casse → `map<string,string>`.
6. La **taille du corps** se déduit de `Content-Length` **ou** `Transfer-Encoding: chunked` — sinon pas de corps.
7. Toujours **valider** et répondre par le **bon code** d'erreur ; toujours mettre des **garde-fous** (taille max, path traversal) car *« resilience is key »*.

Quand ces 7 points sont clairs dans ta tête, on attaque le parseur ensemble.
