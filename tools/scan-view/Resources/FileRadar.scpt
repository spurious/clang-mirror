FasdUAS 1.101.10   ��   ��    k             i         I     �� 	��
�� .aevtoappnull  �   � **** 	 o      ���� 0 argv  ��    k     � 
 
     r         n         1    ��
�� 
leng  o     ���� 0 argv    o      ���� 0 n N      r    	    m       �      o      ���� 0 res        Z   
   ����  A   
     o   
 ���� 0 n N  m    ����   R    �� ��
�� .ascrerr ****      � ****  m       �   � U s a g e :   F i l e R a d a r   c o m p o n e n t   c o m p o n e n t V e r s i o n   p e r s o n I D   p r o b l e m T i t l e   d e s c r i p t i o n   d i a g   c o n f i g   { f i l e s } *��  ��  ��       !   r     " # " n     $ % $ 4    �� &
�� 
cobj & m    ����  % o    ���� 0 argv   # o      ���� "0 mycomponentname myComponentName !  ' ( ' r     & ) * ) n     $ + , + 4   ! $�� -
�� 
cobj - m   " #����  , o     !���� 0 argv   * o      ���� (0 mycomponentversion myComponentVersion (  . / . r   ' - 0 1 0 n   ' + 2 3 2 4   ( +�� 4
�� 
cobj 4 m   ) *����  3 o   ' (���� 0 argv   1 o      ���� 0 
mypersonid 
myPersonID /  5 6 5 r   . 4 7 8 7 n   . 2 9 : 9 4   / 2�� ;
�� 
cobj ; m   0 1����  : o   . /���� 0 argv   8 o      ����  0 myproblemtitle myProblemTitle 6  < = < r   5 ; > ? > n   5 9 @ A @ 4   6 9�� B
�� 
cobj B m   7 8����  A o   5 6���� 0 argv   ? o      ���� 0 mydescription myDescription =  C D C r   < B E F E n   < @ G H G 4   = @�� I
�� 
cobj I m   > ?����  H o   < =���� 0 argv   F o      ���� 0 mydiag myDiag D  J K J r   C K L M L n   C G N O N 4   D G�� P
�� 
cobj P m   E F����  O o   C D���� 0 argv   M o      ���� 0 myconfig myConfig K  Q R Q Z   L l S T�� U S ?   L O V W V o   L M���� 0 n N W m   M N����  T r   R c X Y X n   R _ Z [ Z 7  S _�� \ ]
�� 
cobj \ m   W [����  ] o   \ ^���� 0 n N [ o   R S���� 0 argv   Y o      ���� 0 myfiles myFiles��   U r   f l ^ _ ^ J   f h����   _ o      ���� 0 myfiles myFiles R  ` a ` r   m � b c b I   m ��� d���� 0 submitreport submitReport d  e f e o   n o���� "0 mycomponentname myComponentName f  g h g o   o p���� (0 mycomponentversion myComponentVersion h  i j i o   p q���� 0 
mypersonid 
myPersonID j  k l k o   q r����  0 myproblemtitle myProblemTitle l  m n m o   r s���� 0 mydescription myDescription n  o p o o   s t���� 0 mydiag myDiag p  q r q o   t w���� 0 myconfig myConfig r  s�� s o   w z���� 0 myfiles myFiles��  ��   c o      ���� 0 res   a  t�� t L   � � u u o   � ����� 0 res  ��     v w v l     ��������  ��  ��   w  x y x l     ��������  ��  ��   y  z { z i     | } | I      �� ~���� 0 submitreport submitReport ~   �  o      ���� "0 mycomponentname myComponentName �  � � � o      ���� (0 mycomponentversion myComponentVersion �  � � � o      ���� 0 
mypersonid 
myPersonID �  � � � o      ����  0 myproblemtitle myProblemTitle �  � � � o      ���� 0 mydescription myDescription �  � � � o      ���� 0 mydiag myDiag �  � � � o      ���� 0 myconfig myConfig �  ��� � o      ���� 0 myfiles myFiles��  ��   } O    X � � � k   W � �  � � � l    � � � � r     � � � m    ����  � o      ���� ,0 myclassificationcode myClassificationCode � � { 1=security; 2=crash/hang/data loss; 3=performance; 4=UI/usability; 6=serious; 7=other; 9=feature; 10=enhancement; 12=task;    � � � � �   1 = s e c u r i t y ;   2 = c r a s h / h a n g / d a t a   l o s s ;   3 = p e r f o r m a n c e ;   4 = U I / u s a b i l i t y ;   6 = s e r i o u s ;   7 = o t h e r ;   9 = f e a t u r e ;   1 0 = e n h a n c e m e n t ;   1 2 = t a s k ; �  � � � r     � � � m    	����  � o      ���� .0 myreproducibilitycode myReproducibilityCode �  � � � l    � � � � r     � � � m    ��
�� boovfals � o      ���� 0 savenewprob saveNewProb �   true or false    � � � �    t r u e   o r   f a l s e �  � � � l   ��������  ��  ��   �  � � � l   �� � ���   � h b- formType & severityCode are no longer used in Radar 6.3 and later. If passed, they are ignored.:    � � � � � -   f o r m T y p e   &   s e v e r i t y C o d e   a r e   n o   l o n g e r   u s e d   i n   R a d a r   6 . 3   a n d   l a t e r .   I f   p a s s e d ,   t h e y   a r e   i g n o r e d . : �  � � � l   �� � ���   � e _ set myFormType to 99 -- This parameter is no longer used in Radar 6.3 and later, it's ignored.    � � � � �   s e t   m y F o r m T y p e   t o   9 9   - -   T h i s   p a r a m e t e r   i s   n o   l o n g e r   u s e d   i n   R a d a r   6 . 3   a n d   l a t e r ,   i t ' s   i g n o r e d . �  � � � l   �� � ���   � i c set mySeverityCode to 99 -- This parameter is no longer used in Radar 6.3 and later, it's ignored.    � � � � �   s e t   m y S e v e r i t y C o d e   t o   9 9   - -   T h i s   p a r a m e t e r   i s   n o   l o n g e r   u s e d   i n   R a d a r   6 . 3   a n d   l a t e r ,   i t ' s   i g n o r e d . �  � � � l   �� � ���   �  -    � � � �  - �  � � � l   ��������  ��  ��   �  � � � r     � � � m     � � � � �   � o      ���� 0 res   �  � � � Z   T � ��� � � m    ��
�� boovfals � k    � � �  � � � r    ! � � � b     � � � b     � � � b     � � � o    ���� 0 res   � m     � � � � �  C o m p o n e n t :   � o    ���� "0 mycomponentname myComponentName � m     � � � � �  
 � o      ���� 0 res   �  � � � r   " + � � � b   " ) � � � b   " ' � � � b   " % � � � o   " #���� 0 res   � m   # $ � � � � � & C o m p o n e n t   V e r s i o n :   � o   % &���� (0 mycomponentversion myComponentVersion � m   ' ( � � � � �  
 � o      ���� 0 res   �  � � � r   , 5 � � � b   , 3 � � � b   , 1 � � � b   , / � � � o   , -���� 0 res   � m   - . � � � � �  P e r s o n   I D :   � o   / 0���� 0 
mypersonid 
myPersonID � m   1 2 � � � � �  
 � o      ���� 0 res   �  � � � r   6 ; � � � b   6 9 � � � o   6 7���� 0 res   � m   7 8 � � � � �  - - 
 � o      ���� 0 res   �  � � � r   < E � � � b   < C �  � b   < A b   < ? o   < =���� 0 res   m   = > �  T i t l e :   o   ? @����  0 myproblemtitle myProblemTitle  m   A B �  
 � o      ���� 0 res   � 	
	 r   F O b   F M b   F K b   F I o   F G���� 0 res   m   G H �  D e s c r i p t i o n :   o   I J���� 0 mydescription myDescription m   K L �  
 o      ���� 0 res  
  r   P [ b   P Y b   P U b   P S  o   P Q���� 0 res    m   Q R!! �""  D i a g n o s i s :   o   S T���� 0 mydiag myDiag m   U X## �$$  
 o      ���� 0 res   %&% r   \ i'(' b   \ g)*) b   \ c+,+ b   \ a-.- o   \ ]���� 0 res  . m   ] `// �00  C o n f i g :  , o   a b���� 0 myconfig myConfig* m   c f11 �22  
( o      ���� 0 res  & 343 r   j w565 b   j u787 b   j q9:9 b   j o;<; o   j k���� 0 res  < m   k n== �>> * C l a s s i f i c a t i o n   C o d e :  : o   o p���� ,0 myclassificationcode myClassificationCode8 m   q t?? �@@  
6 o      ���� 0 res  4 ABA r   x �CDC b   x �EFE b   x GHG b   x }IJI o   x y�� 0 res  J m   y |KK �LL , R e p r o d u c i b i l i t y   C o d e :  H o   } ~�~�~ .0 myreproducibilitycode myReproducibilityCodeF m    �MM �NN  
D o      �}�} 0 res  B O�|O Y   � �P�{QR�zP r   � �STS b   � �UVU b   � �WXW b   � �YZY b   � �[\[ b   � �]^] o   � ��y�y 0 res  ^ m   � �__ �`` 
 F i l e  \ o   � ��x�x 0 i  Z m   � �aa �bb  :  X n   � �cdc 4   � ��we
�w 
cobje o   � ��v�v 0 i  d o   � ��u�u 0 myfiles myFilesV m   � �ff �gg  
T o      �t�t 0 res  �{ 0 i  Q m   � ��s�s R l  � �h�r�qh n   � �iji 1   � ��p
�p 
lengj o   � ��o�o 0 myfiles myFiles�r  �q  �z  �|  ��   � k   �Tkk lml r   � �non I  � ��n�mp
�n .rdrlnprb****    ��� obj �m  p �lqr
�l 
descq l 
 � �s�k�js o   � ��i�i 0 mydescription myDescription�k  �j  r �htu
�h 
diagt l 
 � �v�g�fv o   � ��e�e 0 mydiag myDiag�g  �f  u �dwx
�d 
cmvrw l 
 � �y�c�by o   � ��a�a (0 mycomponentversion myComponentVersion�c  �b  x �`z{
�` 
cmnmz l 
 � �|�_�^| o   � ��]�] "0 mycomponentname myComponentName�_  �^  { �\}~
�\ 
rcod} l 
 � ��[�Z o   � ��Y�Y .0 myreproducibilitycode myReproducibilityCode�[  �Z  ~ �X��
�X 
orig� l 
 � ���W�V� o   � ��U�U 0 
mypersonid 
myPersonID�W  �V  � �T��
�T 
pbti� l 
 � ���S�R� o   � ��Q�Q  0 myproblemtitle myProblemTitle�S  �R  � �P��
�P 
conf� l 
 � ���O�N� o   � ��M�M 0 myconfig myConfig�O  �N  � �L��
�L 
clcd� l 
 � ���K�J� o   � ��I�I ,0 myclassificationcode myClassificationCode�K  �J  � �H��G
�H 
dsav� o   � ��F�F 0 savenewprob saveNewProb�G  o o      �E�E 0 bugid bugIDm ��� r   � ���� b   � ���� b   � ���� b   � ���� b   � ���� b   � ���� b   � ���� o   � ��D�D 0 res  � m   � ��� ��� . F i l e d :   < a   h r e f = " r d a r : / /� o   � ��C�C 0 bugid bugID� m   � ��� ���  / " >� o   � ��B�B 0 bugid bugID� m   � ��� ���  < / a >� m   � ��� ���  
� o      �A�A 0 res  � ��� l  � ��@�?�>�@  �?  �>  � ��� Y   �8��=���<� k  3�� ��� r   ��� b  ��� b  ��� b  ��� o  �;�; 0 res  � m  �� ���  A t t a c h e d :  � l ��:�9� n  ��� 4  �8�
�8 
cobj� o  �7�7 0 i  � o  �6�6 0 myfiles myFiles�:  �9  � m  �� ���  
� o      �5�5 0 res  � ��4� I !3�3�2�
�3 .rdrladdfutxt       obj �2  � �1��
�1 
mr11� l %+��0�/� n  %+��� 4  &+�.�
�. 
cobj� o  )*�-�- 0 i  � o  %&�,�, 0 myfiles myFiles�0  �/  � �+��*
�+ 
pbid� o  ./�)�) 0 bugid bugID�*  �4  �= 0 i  � m  �(�( � l 	��'�&� n  	��� 1  �%
�% 
leng� o  �$�$ 0 myfiles myFiles�'  �&  �<  � ��� l 99�#�"�!�#  �"  �!  � ��� r  9D��� b  9B��� b  9>��� o  9:� �  0 res  � m  :=�� ��� 6 A d d e d   k e y w o r d :   L L V M _ C H E C K E R� m  >A�� ���  
� o      �� 0 res  � ��� I ET���
� .rdrladkilong       obj �  � ���
� 
pbid� o  IJ�� 0 bugid bugID� ���
� 
keyi� m  MP��  �  �   � ��� L  UW�� o  UV�� 0 res  �   � m     ���                                                                                  RADR   alis    8  Leopard                    ĚuH+     �	Radar.app                                                       1V����%        ����  	                Applications    Ě��      ��_�       �  Leopard:Applications:Radar.app   	 R a d a r . a p p    L e o p a r d  Applications/Radar.app  / ��   { ��� l     ����  �  �  �       �������������������  � �
�	��������� ����������
�
 .aevtoappnull  �   � ****�	 0 submitreport submitReport� 0 n N� 0 res  � "0 mycomponentname myComponentName� (0 mycomponentversion myComponentVersion� 0 
mypersonid 
myPersonID�  0 myproblemtitle myProblemTitle� 0 mydescription myDescription� 0 mydiag myDiag�  0 myconfig myConfig�� 0 myfiles myFiles��  ��  ��  ��  � �� ��������
�� .aevtoappnull  �   � ****�� 0 argv  ��  � ���� 0 argv  � ���� ���� ����������������������������
�� 
leng�� 0 n N�� 0 res  �� 
�� 
cobj�� "0 mycomponentname myComponentName�� (0 mycomponentversion myComponentVersion�� 0 
mypersonid 
myPersonID�� ��  0 myproblemtitle myProblemTitle�� �� 0 mydescription myDescription�� �� 0 mydiag myDiag�� 0 myconfig myConfig�� �� 0 myfiles myFiles�� 0 submitreport submitReport�� ���,E�O�E�O�� 	)j�Y hO��k/E�O��l/E�O��m/E�O���/E�O���/E�O���/E�O���/E` O�� �[�\[Za \Z�2E` Y jvE` O*������_ _ a + E�O�� �� }���������� 0 submitreport submitReport�� ����� �  ������������������ "0 mycomponentname myComponentName�� (0 mycomponentversion myComponentVersion�� 0 
mypersonid 
myPersonID��  0 myproblemtitle myProblemTitle�� 0 mydescription myDescription�� 0 mydiag myDiag�� 0 myconfig myConfig�� 0 myfiles myFiles��  � ������������������������������ "0 mycomponentname myComponentName�� (0 mycomponentversion myComponentVersion�� 0 
mypersonid 
myPersonID��  0 myproblemtitle myProblemTitle�� 0 mydescription myDescription�� 0 mydiag myDiag�� 0 myconfig myConfig�� 0 myfiles myFiles�� ,0 myclassificationcode myClassificationCode�� .0 myreproducibilitycode myReproducibilityCode�� 0 savenewprob saveNewProb�� 0 res  �� 0 i  �� 0 bugid bugID� 6����� � � � � � � � �!#/1=?KM��_a��f���������������������������������������������� �� 
�� 
leng
�� 
cobj
�� 
desc
�� 
diag
�� 
cmvr
�� 
cmnm
�� 
rcod
�� 
orig
�� 
pbti
�� 
conf
�� 
clcd
�� 
dsav�� 
�� .rdrlnprb****    ��� obj 
�� 
mr11
�� 
pbid
�� .rdrladdfutxt       obj 
�� 
keyi��  
�� .rdrladkilong       obj ��Y�U�E�O�E�OfE�O�E�Of ���%�%�%E�O��%�%�%E�O��%�%�%E�O��%E�O��%�%�%E�O��%�%�%E�O��%�%a %E�O�a %�%a %E�O�a %�%a %E�O�a %�%a %E�O +k�a ,Ekh �a %�%a %�a �/%a %E�[OY��Y �*a �a �a �a �a  �a !�a "�a #�a $�a %�a & 'E�O�a (%�%a )%�%a *%a +%E�O 8k�a ,Ekh �a ,%�a �/%a -%E�O*a .�a �/a /�� 0[OY��O�a 1%a 2%E�O*a /�a 3a 4� 5O�U� � ��� F i l e d :   < a   h r e f = " r d a r : / / 6 2 3 3 8 8 1 / " > 6 2 3 3 8 8 1 < / a > 
 A t t a c h e d :   / t m p / s c a n - b u i l d - 2 0 0 8 - 0 9 - 1 8 - 2 / r e p o r t - s h E s U u . h t m l 
 A d d e d   k e y w o r d :   L L V M _ C H E C K E R 
� ��� 8 B u g s   f o u n d   b y   c l a n g   A n a l y z e r� ���  X� ���  � ���  d e a d   s t o r e   5 5 5 5� ���  2 5 4 2 3 5� ���  � ���  � ����� �  �� ��� ^ / t m p / s c a n - b u i l d - 2 0 0 8 - 0 9 - 1 8 - 2 / r e p o r t - s h E s U u . h t m l�  �  �  �   ascr  ��ޭ