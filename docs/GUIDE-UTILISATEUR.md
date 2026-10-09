---
title: Guide utilisateur
nav_order: 2
---

# Guide d'utilisation — Balance Silo

Ce document est pour **qui utilise la balance au quotidien** : ce qu'on voit sur
l'écran, ce qu'il faut faire, et ce qui se passe ensuite. Il ne parle ni de code,
ni de compilation, ni de câblage.

Pour le développement, voir [`SPEC-ecrans.md`](SPEC-ecrans.md) (machine à états,
effets de bord, rafraîchissements), [`FONCTIONNEMENT.md`](FONCTIONNEMENT.md) et le
[`README`](https://github.com/baptistev7/ESPScale/blob/main/README.md).

---

## 1. Le principe : un bouton, deux gestes

La balance a **un seul bouton**. Tout se joue sur la durée de l'appui :

| Geste | Durée | Comment il est écrit à l'écran |
|---|---|---|
| **appui court** | moins d'1 s | `● COURT` |
| **appui long** | plus d'1 s | `— LONG` |
| **maintien** | 2,5 s, une barre se remplit | `— LONG` seul, avec une barre |

**Le pied de chaque écran annonce exactement les gestes possibles.** C'est la
règle de l'interface : si un geste n'est pas écrit en bas de l'écran, il ne fait
rien de visible. Un appui court sur l'écran du mode nomade, par exemple, ne
change rien à l'écran — et il n'est donc **pas** annoncé non plus.

> ⚠️ Le libellé `● COURT` signifie « un appui **court** », pas « un second
> appui après un appui long ». Un appui long n'ouvre pas de menu caché.

---

## 2. L'écran principal

C'est l'écran que vous voyez au réveil, quand la balance est posée sur sa base.

| Niveau normal | Niveau bas | En charge (USB) |
|---|---|---|
| <img src="screens/main.png" width="150" alt="Écran principal"> | <img src="screens/main_low.png" width="150" alt="Niveau bas"> | <img src="screens/main_charge.png" width="150" alt="En charge"> |

- **En-tête, sur tous les écrans** : la date à gauche, l'éclair si le câble USB
  est branché, la batterie à droite.
- **L'état** sous l'en-tête : `● CONNECTÉ` quand la mesure est bonne, `● KO`
  quand un ou plusieurs pieds ne répondent pas. Le bandeau noir dit alors
  `CAPTEUR(S) HS`, et le poids affiché est la **dernière mesure stable** — la
  zone du bas le rappelle, avec son heure (`DERNIÈRE MESURE` /
  `STABLE À 18:12`). Ce n'est pas le poids actuel.

| Capteur(s) HS |
|---|
| <img src="screens/main_degraded.png" width="150" alt="Capteur(s) HS, dernière mesure stable"> |
- **La zone du bas** dit l'état de la mesure :
  - `MESURÉ À 18:12` — la mesure est faite, rien à signaler ; l'heure est celle
    de la dernière mesure (`MESURE STABLE` si l'heure n'est pas encore connue) ;
  - `ENVOI EN ERREUR` — la mesure n'a **pas** pu être envoyée à Home
    Assistant. En haut de l'écran, `CONNECTÉ` devient `WIFI DÉCONNECTÉ` (WiFi
    absent) ou `MQTT DÉCONNECTÉ` (broker qui refuse). Le poids affiché est bon,
    mais HA ne l'a pas reçu ; l'envoi est retenté à la prochaine mesure, et le
    message disparaît dès qu'il réussit ;
  - `REMPLISSAGE CONSEILLE`, avec le bandeau `NIVEAU BAS` — le poids est passé
    **sous le seuil « niveau bas »** que vous avez défini dans le portail. Il ne
    reste plus grand-chose.
- **La jauge** donne le taux de remplissage du silo (en %), et le poids en
  gros. Le zéro est celui de la calibration des 4 pieds (portail, onglet
  CALIBRATION).

**Les deux gestes :**

| Geste | Effet |
|---|---|
| `● COURT MESURER` | refait une mesure **immédiatement**, et publie le poids à Home Assistant s'il a changé |
| `— LONG OPTIONS` | ouvre le menu (voir §4) |

> La mesure automatique a lieu deux fois par jour, **30 min avant et 30 min après
> l'aspiration quotidienne** (18h30 par défaut → mesures à 18h00 et 19h00).
> `MESURER` est là quand vous ne voulez pas attendre.

---

## 3. Le remplissage (ajouter des granulés)

C'est l'opération principale. Elle se déclenche **depuis la balance**, jamais
depuis Home Assistant : Home Assistant ne voit que le résultat.

Depuis l'écran principal : `— LONG OPTIONS`, puis choisissez `REMPLISSAGE`
(`● COURT` pour descendre, `— LONG` pour valider).

### Les écrans du parcours

```
  ┌──> 1/3 EN COURS  ──● court──>  2/3 NOMBRE DE SACS  ──long──>  3/3 PRIX
  │      le poids                  ● court = +1 sac        ● court = +1
  │      se voit en direct         long = valider           long = suivant
  │                                                             │
  │        ┌──────────────  maintien 2,5 s  ─────────────────┘
  │        ▼
  │   4/3 TERMINER ?  ──● court──> CORRIGER ? ──> sacs / prix ──> TERMINER ?
  │        │       (lâcher en cours = la barre       ANNULER ──> écran principal
  │        │        se vide, rien d'autre)           RETOUR  ──> TERMINER ?
  │        │
  └───────────┴── 5/3 ENREGISTRÉ (1 min) ──● court──> écran principal
```

| 1/3 En cours | 2/3 Sacs | 3/3 Prix | Terminer ? | Enregistré |
|---|---|---|---|---|
| <img src="screens/refill_1.png" width="110" alt="Remplissage, en cours"> | <img src="screens/refill_2.png" width="110" alt="Nombre de sacs"> | <img src="screens/refill_3.png" width="110" alt="Prix par sac"> | <img src="screens/refill_confirm_5.png" width="110" alt="Terminer, maintien en cours"> | <img src="screens/refill_saved.png" width="110" alt="Enregistré"> |

**1/3 — EN COURS.** Le poids versé s'affiche pendant que vous versez : la balance
sonde toutes les 3 secondes et ne redessine qu'au-delà de 0,5 kg. Quand vous avez
versé, `● COURT` passe à la suite.

**2/3 — NOMBRE DE SACS.** `● COURT` ajoute un sac (aussi longtemps que vous
voulez, un sac par appui), `— LONG VALIDER` passe à la suite. La ligne
`Théorie : N kg` est calculée avec le **poids d'un sac** que vous avez défini dans
le portail (15 kg par défaut) : c'est un repère pour comparer au poids versé, pas
une mesure. Si vos sacs pèsent 12 kg, indiquez 12 là-bas.

**3/3 — PRIX PAR SAC.** Saisie **par chiffres** : le chiffre courant est entouré
d'un cadre. `● COURT PLUS` l'augmente de 1 (après 9, il revient à 0), `— LONG
SUIVANT` passe au chiffre suivant, et sur le dernier `— LONG VALIDER` valide le
prix. Le prix se saisit **après** le versement, quand vous savez ce que vous avez
payé.

**4/3 — TERMINER ?** L'écran récapitule ce qui va être enregistré : poids
réellement ajouté, nombre de sacs, coût. **Il faut TENIR 2,5 s** — une barre de
maintien se remplit. Si vous lâchez avant la fin, la barre se vide simplement :
rien n'est enregistré, vous pouvez recommencer. Un **appui court** ouvre
**« CORRIGER ? »** (rien n'est enregistré non plus) :

- `NOMBRE DE SACS` — on recompte les sacs depuis 0 (le prix est gardé) ;
- `PRIX DU SAC` — on ressaisit le prix (curseur sur le premier chiffre) ;
- `ANNULER` — on abandonne la déclaration : le versement est quand même vu par
  la mesure suivante et envoyé en **remplissage sauvage** (sans sacs ni prix) ;
- `RETOUR` — on revient au récap sans rien changer.

`● COURT` passe au choix suivant, `— LONG` le valide. Après une correction, le
récap se réaffiche avec les nouvelles valeurs — il faut à nouveau le tenir
2,5 s. Rien n'est jamais envoyé sans ce maintien.

<img src="screens/refill_fix_0.png" width="110" alt="Corriger ?">

**5/3 — ENREGISTRÉ.** Le récap : sacs, coût, prix au kilo, poids total. Il
s'affiche **1 minute**, puis la balance revient à l'écran principal **sans
re-mesurer**.

> ℹ️ **Abandon.** Sans geste pendant 10 minutes à n'importe quelle étape (ou si
> le poids a **baissé** pendant le versement), le parcours est abandonné : aucun
> prix n'est envoyé. La balance refait aussitôt une mesure normale et revient à
> l'écran principal. Si vous aviez versé au moins ~1 sac (14 kg), cette hausse
> part à Home Assistant comme un **appoint non déclaré** (`bag_count` à 0,
> valorisé au prix moyen du stock).

### Si vous déplacez la balance pendant le remplissage

Pas de souci : elle s'arrête, note où elle en était, et reprend quand vous la
reposez. Si vous ne la reposez pas dans l'heure, le remplissage est **annulé**
et tout repart de zéro.

---

## 4. Le menu OPTIONS

`— LONG OPTIONS` sur l'écran principal (ou sur l'écran nomade). **4 entrées,
toujours les mêmes, toujours dans le même ordre** :

```
  REMPLISSAGE
  PORTAIL RÉGLAGES
  INFORMATIONS
  FERMER
```

| Sur la base | Hors de la base |
|---|---|
| <img src="screens/options_0.png" width="150" alt="Menu OPTIONS"> | <img src="screens/options_dedock_dither.png" width="150" alt="Menu hors base, REMPLISSAGE indisponible"> |

- `● COURT SUIVANT` — une ligne par appui ; le curseur **saute** les lignes
  indisponibles.
- `— LONG CHOISIR` — ouvre la ligne sélectionnée.

`FERMER` vous ramène à l'écran du mode courant. **Sans geste pendant 1 minute**,
c'est pareil : la balance ne reste pas bloquée sur le menu.

---

## 5. L'écran INFORMATIONS (2 pages)

`OPTIONS → INFORMATIONS`.

- **Page 1 — état général** : batterie, Wi-Fi, MQTT, force du signal, état de la
  dernière mesure, nombre de capteurs qui répondent.
- **Page 2 — CAPTEURS** : le poids lu par **chacun des 4 pieds** et le total.

| Page 1 — état général | Page 2 — capteurs |
|---|---|
| <img src="screens/infos_1.png" width="150" alt="Informations, page 1"> | <img src="screens/infos_2.png" width="150" alt="Informations, page 2"> |

| Geste | Effet |
|---|---|
| `● COURT AUTRE PAGE` | page 1 ↔ page 2, autant de fois que voulu |
| `— LONG RETOUR` | **retour au menu OPTIONS** |

> Revenir des informations ne vous fait pas sortir du menu : c'est une
> consultation, on peut y aller et en revenir autant de fois qu'on veut. Sans
> geste pendant 1 minute, on revient aussi au menu.

---

## 6. Le mode nomade (balance hors de sa base)

Quand les 4 pieds ne répondent pas, la balance affiche **MODE NOMADE** : elle
sait qu'elle n'est plus sur sa base.

| Avec une mesure connue | Sans mesure |
|---|---|
| <img src="screens/nomade.png" width="150" alt="Mode nomade"> | <img src="screens/nomade_none.png" width="150" alt="Mode nomade, aucune mesure"> |

L'écran rappelle le **dernier poids connu**, l'heure de cette mesure et son
ancienneté : ce n'est **pas** une mesure actuelle (bandeau `NON CONNECTÉ`).

- **Un appui court ne fait rien de visible** — et il n'est donc pas annoncé.
  Il relit seulement l'état de charge.
- `— LONG OPTIONS` ouvre quand même le menu.
- La seule entrée qui change : `REMPLISSAGE` devient indisponible (elle est
  grisée et le curseur la saute). Inutile d'essayer d'y entrer, elle ne peut pas
  fonctionner sans les capteurs.
- En nomade, la balance dort jusqu'à **minuit** (la date de l'écran change,
  elle refait alors un contrôle complet). **En charge**, elle se réveille
  **toutes les 5 minutes** pour montrer la jauge qui monte. Brancher ou
  débrancher l'USB la réveille : l'éclair apparaît ou disparaît en quelques
  secondes.
- Remettez-la sur sa base : elle mesure, publie le retour sur la base (et le
  poids s'il a changé), puis repasse en veille normale. Voir §9 pour le délai.

---

## 7. Le portail de configuration

Pour changer le réseau, le broker MQTT, le silo, l'heure d'aspiration ou
calibrer les pieds : il faut un ordinateur ou un téléphone, pas l'écran de la
balance.

Deux situations :

- **La balance n'a aucune configuration** (neuve, ou remise à zéro) : elle
  démarre directement en portail, toute seule.
- **Elle est déjà configurée** : `OPTIONS → PORTAIL RÉGLAGES`, puis **tenir 2,5 s**
  sur l'écran de confirmation.

L'écran affiche alors `PORTAIL ACTIF` avec le nom du réseau à rejoindre
(`Scale-XXXX`), son **mot de passe** (8 chiffres, nouveau à chaque ouverture) et
l'adresse de la page. Il faut donc être devant la balance pour s'y connecter.
Les mots de passe déjà enregistrés (WiFi, MQTT) ne sont **jamais** réaffichés
dans la page : un champ laissé vide veut dire « inchangé ».

| Confirmation (tenir 2,5 s) | Portail actif | Sans configuration |
|---|---|---|
| <img src="screens/portal_confirm_5.png" width="150" alt="Confirmation du portail"> | <img src="screens/portail.png" width="150" alt="Portail actif"> | <img src="screens/portail_non_configure.png" width="150" alt="Portail actif, pied VEILLE"> |

La page a **3 onglets** :

- **ÉTAT** — ce que la balance sait maintenant : poids, niveau, batterie, dernier
  envoi, heure d'aspiration, heures des prochaines mesures. Les valeurs inconnues
  affichent `--` plutôt qu'un chiffre inventé.
- **RÉGLAGES** — WiFi, broker, silo (capacité, seuil « niveau bas », poids d'un
  sac) et horaire d'aspiration, **dans un seul formulaire** : un bouton
  **« Enregistrer et redémarrer »** suffit. La connexion WiFi est testée avant
  écriture : si elle échoue, **rien** n'est enregistré. Le bouton **« Tester
  WiFi + MQTT »**, dans la section du broker, vérifie seulement la connexion au
  réseau et au broker avec les valeurs saisies — il n'écrit rien.
- **CALIBRATION** — la calibration des 4 pieds.

| ÉTAT | RÉGLAGES | CALIBRATION |
|---|---|---|
| <img src="screens/portal_etat.png" width="220" alt="Portail, onglet ÉTAT"> | <img src="screens/portal_reglages.png" width="220" alt="Portail, onglet RÉGLAGES"> | <img src="screens/portal_calibration.png" width="220" alt="Portail, onglet CALIBRATION"> |

(Captures avec des valeurs d'exemple.)

Le portail se ferme de **deux** façons : **`● COURT REDEMARRER`** sur la balance,
ou tout seul après 10 minutes d'inactivité. Les deux font redémarrer la carte,
qui reprend ses mesures.

**Balance sans configuration** : le pied annonce alors `● COURT VEILLE`. À la
fermeture du portail, elle se met en veille (elle ne se réveille qu'une fois par
jour pour vérifier sa batterie) et l'écran l'indique : **« CARTE EN VEILLE »**,
bandeau `NON CONFIGURÉ`, pied `● COURT RÉVEIL`. Un appui sur le bouton la
réveille et rouvre le portail.

<img src="screens/veille_non_configure.png" width="150" alt="Carte en veille, non configurée">

---

## 8. Ce que reçoit Home Assistant

La balance **publie**, elle ne reçoit rien. Le poids publié suit un cliquet :

- une **baisse** d'au moins 1 kg est publiée (c'est la consommation) ;
- une **hausse** déclarée par le parcours de remplissage est publiée avec son
  prix ;
- une **hausse non déclarée** d'au moins ~1 sac (14 kg) est publiée comme
  appoint (`refill/event` avec `bag_count` à 0) ;
- une hausse plus petite (dérive, bruit) n'est jamais publiée ;
- le **retour sur la base** est publié (fin d'erreur), même si le poids n'a pas
  bougé.

| Sujet | Contenu |
|---|---|
| `scale/value` | poids net, en kg |
| `scale/battery` | batterie, en % |
| `scale/version` | version du firmware |
| `scale/error/on-off` | `on` = capteurs muets (hors base ou panne), `off` = mesure OK |
| `scale/quality` | `{"valid": n/4, "mode": "…"}` |
| `scale/refill/state` | `pouring` → `pricing` → `saved` |
| `scale/refill/event` | `{"added_kg", "bag_price_eur", "bag_count"}` |

> Le `refill/event` porte **la comptabilité** : coût = `bag_count` ×
> `bag_price_eur`. Un `bag_count` à 0 signale un appoint que vous n'avez pas
> déclaré (hausse d'au moins ~1 sac sans parcours, ou parcours abandonné) :
> Home Assistant le valorise au prix moyen du stock.

---

## 9. Cas rencontrés

**« L'écran affiche un poids qui ne change pas »**
Tant que le poids ne **baisse** pas (ou ne monte pas d'au moins ~1 sac), rien
n'est publié : c'est voulu, pour ne pas noyer Home Assistant de dérive.

**« Je l'ai remise sur sa base, elle ne s'est pas réveillée tout de suite »**
Le retour sur la base réveille la balance **instantanément** par la ligne `DOUT`.
Si ce n'est pas le cas, vérifiez le contact du connecteur pogo : à défaut, le
retour est vu au contrôle complet de **minuit**.

**« Elle affiche une date fausse »**
Le RTC interne est perdu à chaque coupure d'alimentation. La date se recale par
NTP **automatiquement** — mais seulement au réveil où la balance ouvre déjà le
réseau. Une measure à la demande (`● COURT MESURER`) force ce réveil : c'est le
moyen le plus simple de recaler l'heure tout de suite.

**« Les 4 pieds n'affichent pas le même poids »**
C'est normal : c'est pour ça que le silo est posé sur 4 pieds. La page 2 des
informations montre la répartition. Si **un** pied affiche `--`, il ne répond
plus.

**« Le poids affiché est trop élevé / trop bas d'un coup »**
Vérifiez la calibration des 4 pieds (portail, onglet CALIBRATION) : un pied qui
a bougé sur son support ou un zéro refait décale tout le poids. Attention, un
zéro refait décale aussi le poids publié : une baisse est comptée comme de la
consommation par Home Assistant.

**« Comment je reviens en arrière ? »**
Sur chaque écran, `— LONG RETOUR` (ou le maintien demandé) vous ramène. Dans le
remplissage, `● COURT CORRIGER` sur « TERMINER ? » permet de recompter les sacs,
de ressaisir le prix ou d'annuler. Après `ENREGISTRÉ`, on sort en `● COURT`.

**« Un maintien n'a rien fait »**
Lâcher le bouton avant que la barre soit pleine ne fait rien : la barre se vide
et vous pouvez recommencer. Seul un appui **bref** déclenche l'action `● COURT`.

**« L'écran affiche CAPTEUR(S) HS »**
Un ou plusieurs pieds ne répondent plus. Le poids affiché est la **dernière
mesure stable**, avec son heure — pas le poids actuel. Vérifiez les connexions,
puis `● COURT MESURER`.

---

## 10. Ce qui n'est pas fait (à savoir)

- **Pas d'écran miroir sur le portail.** Le portail ne rejoue pas la dalle ; il
  affiche l'état sous forme de valeurs. C'est plus lisible sur un téléphone, et
  cela coûte moins cher en flash.
- **Le portail ne se ferme pas « proprement »** : le bouton fait redémarrer la
  carte, il ne la rend pas au menu. C'est normal — le portail n'a pas de retour
  vers les écrans.
- **Le réveil au redock n'est pas câblé** (ligne `DOUT`). Voir §9.
- **Aucune commande distante.** Le portail ne pilote rien d'autre que le
  rafraîchissement de l'écran. Home Assistant ne peut pas non plus lancer un
  remplissage : le versement se fait à la main.